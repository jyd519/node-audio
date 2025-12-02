#include "recorder_api.h"
#include "addon_api.h"
#include "timestamp.h"
#include "enc_writer.h"

#include <cstring>

// Maximum number of frames to buffer before dropping frames
constexpr int MAX_BUF_FRAMES = 30;

/**
 * Sets the FFmpeg logging level
 * @param info JavaScript call info containing the log level string
 * @return Boolean indicating success
 */
static Napi::Value setFFmpegLoggingLevel(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  // Validate arguments
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "String argument expected").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  std::string level = info[0].ToString();
  av::set_logging_level(level);
  return Napi::Boolean::New(env, true);
}

/**
 * Initialize the Recorder class and export it to JavaScript
 * @param env Node.js environment
 * @param exports Export object
 * @return Updated exports object
 */
Napi::Object Recorder::Init(Napi::Env env, Napi::Object exports) {
  // Define class with instance methods
  Napi::Function func = DefineClass(
      env, "Recorder",
      {
          InstanceMethod<&Recorder::AddImage>(
              "AddImage", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
          InstanceMethod<&Recorder::AddWebm>(
              "AddWebm", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
          InstanceMethod<&Recorder::Close>(
              "Close", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
          InstanceMethod<&Recorder::FramesAdded>(
              "Frames", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
      });

  // Store constructor for future reference
  Napi::FunctionReference *constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  auto pInstance = env.GetInstanceData<InstanceData>();
  pInstance->recorder_ctor = constructor;

  // Initialize FFmpeg
  av::init();
  av::set_logging_level("warning");

  // Export class and utility functions
  exports.Set("Recorder", func);
  exports.Set("SetFFmpegLoggingLevel", Napi::Function::New(env, setFFmpegLoggingLevel));
  return exports;
}

/**
 * Constructor for the Recorder class
 * @param info JavaScript call info containing URI and options
 */
Recorder::Recorder(const Napi::CallbackInfo &info) : Napi::ObjectWrap<Recorder>(info) {
  Napi::Env env = info.Env();

  // Validate arguments
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsObject()) {
    Napi::TypeError::New(env, "Expected URI string and options object")
        .ThrowAsJavaScriptException();
    hasError = true;
    return;
  }

  auto uri = info[0].ToString().Utf8Value();
  auto opts = info[1].ToObject();

  // Default configuration values
  int gop = 0;
  int64_t bitrate = 16000 * 1000;
  std::string format = "mp4";
  std::string movflags = "frag_keyframe+empty_moov";
  std::string encoder_opt = "profile=main;rc_mode=bitrate;allow_skip_frames=true";
  std::string comment;
  std::map<std::string, std::string> metadata;

  // Parse options with structured binding and improved readability
  if (auto v = opts.Get("gop"); v.IsNumber()) {
    gop = v.ToNumber().Int32Value();
  }
  if (auto v = opts.Get("fps"); v.IsNumber()) {
    this->fps = v.ToNumber().Int32Value();
  }
  if (auto v = opts.Get("width"); v.IsNumber()) {
    this->width = v.ToNumber().Int32Value();
  }
  if (auto v = opts.Get("height"); v.IsNumber()) {
    this->height = v.ToNumber().Int32Value();
  }
  if (auto v = opts.Get("bitrate"); v.IsNumber()) {
    bitrate = v.ToNumber().Int64Value();
  }
  if (auto v = opts.Get("comment"); v.IsString()) {
    comment = v.ToString();
  }
  if (auto v = opts.Get("format"); v.IsString()) {
    format = v.ToString();
  }
  if (auto v = opts.Get("movflags"); v.IsString()) {
    movflags = v.ToString();
  }
  if (auto v = opts.Get("encoder_opt"); v.IsString()) {
    encoder_opt = v.ToString();
  }
  if (auto v = opts.Get("password"); v.IsString()) {
    password = v.ToString();
  }
  if (auto v = opts.Get("meta"); v.IsObject()) {
    auto meta = v.ToObject();
    auto properties = meta.GetPropertyNames();
    for (uint32_t i = 0; i < properties.Length(); i++) {
      auto key = properties.Get(i).As<Napi::String>().Utf8Value();
      auto value = meta.Get(key).As<Napi::String>().Utf8Value();
      metadata[key] = value;
    }
  }

  std::error_code ec;

  // Setup output format and encoder
  try {
    ofrmt.setFormat(format, uri);
    auto h264Codec = av::findEncodingCodec(AV_CODEC_ID_H264);
    if (h264Codec.isNull()) {
      throw std::runtime_error("H264 codec not found");
    }

    encoder.setCodec(h264Codec, true);
    octx.setFormat(ofrmt);
    octx.setSocketTimeout(5000); // 5 seconds timeout

    // Configure encoder parameters
    encoder.setWidth(this->width);
    encoder.setHeight(this->height);
    encoder.setPixelFormat(AV_PIX_FMT_YUV420P);
    encoder.setTimeBase({1, fps});
    encoder.setBitRate(bitrate);
    if (gop > 0) {
      encoder.setGopSize(gop);
    }

    // Set global header flag if needed
    if (octx.outputFormat().isFlags(AVFMT_GLOBALHEADER)) {
      encoder.addFlags(AV_CODEC_FLAG_GLOBAL_HEADER);
    }

    // Parse encoder options
    av::Dictionary options;
    options.parseString(encoder_opt, "=", ";", 0, ec);
    if (ec) {
      av_log(nullptr, AV_LOG_WARNING, "Can't parse encoder options: %s\n", ec.message().c_str());
    }

    // Open encoder
    encoder.open(options, {}, ec);
    if (ec) {
      av_log(nullptr, AV_LOG_ERROR, "Can't open encoder: %s\n", ec.message().c_str());
      hasError = true;
      return;
    }

    // Open output context
    if (password.empty() || uri.find("://") != std::string::npos) {
      octx.openOutput(uri, ec);
    } else {
      // We can only encrypt local file
      auto writer = new EncryptWriter(uri, password);
      owriter.reset(writer);
      octx.openOutput(writer, ec);
    }
    if (ec) {
      av_log(nullptr, AV_LOG_ERROR, "Can't open output: %s\n", ec.message().c_str());
      hasError = true;
      return;
    }
  } catch (const std::exception &e) {
    av_log(nullptr, AV_LOG_ERROR, "Exception during setup: %s\n", e.what());
    hasError = true;
    return;
  }

  try {
    // Add video stream
    av::Stream ost = octx.addStream(encoder);

    // Set frame rate
    ost.setFrameRate(av::Rational{fps, 1});
    ost.setTimeBase(av::Rational{1, fps});

    // Initialize video rescaler
    rescaler = av::VideoRescaler(encoder.width(), encoder.height(), encoder.pixelFormat());

    // Debug output of format context
    if (av_log_get_level() >= AV_LOG_DEBUG) {
      octx.dump();
    }

    // Handle metadata
    av::Dictionary metaDic = av::Dictionary(octx.raw()->metadata, false);
    for (const auto &kv : metadata) {
      metaDic.set(kv.first, kv.second);
    }

    // Add creation time if not present
    if (!metaDic.get("creation_time")) {
      time_t rawtime{};
      struct tm timeinfo{};
      char buffer[80] = {0};
      time(&rawtime);
#ifdef _WIN32
      gmtime_s(&timeinfo, &rawtime);
#else
      gmtime_r(&rawtime, &timeinfo);
#endif
      strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
      metaDic.set("creation_time", buffer);
    }

    // Add comment if provided
    if (!comment.empty()) {
      metaDic.set("comment", comment);
    }

    // Transfer metadata ownership if needed
    if (metaDic.isOwning()) {
      octx.raw()->metadata = metaDic.release();
    }

    // Set movflags and write header
    av::Dictionary options;
    options.set("movflags", movflags);
    octx.writeHeader(options);
    octx.flush();

    // Initialize frame buffer
    {
      std::unique_lock<std::mutex> lock(mtx_frames);
      done = false;
      frames.clear();
    }

    // Initialize flush timing
    last_flush_time = std::chrono::steady_clock::now();

    // Start processing thread
    thrd = std::thread(&Recorder::process_frames, this);
  } catch (const std::exception &e) {
    av_log(nullptr, AV_LOG_ERROR, "Exception during stream setup: %s\n", e.what());
    hasError = true;
  }
}

/**
 * Destructor - ensures thread is stopped and resources are cleaned up
 */
Recorder::~Recorder() {
  stop_thread();

  // Clear rescaler cache
  std::lock_guard<std::mutex> lock(mtx_rescaler_cache);
  rescaler_cache.clear();
}

/**
 * Close the recorder and stop the processing thread
 * @param info JavaScript call info
 * @return Boolean indicating success
 */
Napi::Value Recorder::Close(const Napi::CallbackInfo &info) {
  stop_thread();
  return Napi::Boolean::New(info.Env(), true);
}

/**
 * Add an image frame to the video
 * @param info JavaScript call info containing image buffer and dimensions
 * @return Boolean indicating success
 */
Napi::Value Recorder::AddImage(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (hasError || done) {
    return Napi::Boolean::New(env, false);
  }
  if (info.Length() < 3) {
    Napi::TypeError::New(env, "Expected at least 3 arguments: buffer, width, height [, duration]")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  if (!info[0].IsBuffer()) {
    Napi::TypeError::New(env, "First argument must be a buffer").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  if (!info[1].IsNumber() || !info[2].IsNumber()) {
    Napi::TypeError::New(env, "Width and height must be numbers").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  auto buf = info[0].As<Napi::Buffer<uint8_t>>();
  auto width_im = info[1].ToNumber().Int32Value();
  auto height_im = info[2].ToNumber().Int32Value();
  if (width_im <= 0 || height_im <= 0 ||
      buf.ByteLength() < static_cast<size_t>(width_im * height_im * 4)) {
    Napi::RangeError::New(env, "Invalid dimensions or buffer too small")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  // Check if duration is provided (optional 4th argument)
  int64_t frame_duration = 0;
  if (info.Length() >= 4) {
    if (!info[3].IsNumber()) {
      Napi::TypeError::New(env, "Duration must be a number").ThrowAsJavaScriptException();
      return env.Undefined();
    }
    frame_duration = info[3].ToNumber().Int64Value();
    if (frame_duration < 0) {
      Napi::RangeError::New(env, "Duration must be non-negative")
          .ThrowAsJavaScriptException();
      return env.Undefined();
    }
  }
  try {
    av::VideoFrame frame = av::VideoFrame(static_cast<const uint8_t *>(buf.Data()),
                                          buf.ByteLength(), AV_PIX_FMT_RGBA, width_im, height_im);
    // Set duration if provided
    if (frame_duration > 0) {
      frame.raw()->duration = frame_duration;
    }
    push(std::move(frame));
    return Napi::Boolean::New(env, !hasError);
  } catch (const std::exception &e) {
    Napi::Error::New(env, std::string("Failed to add image: ") + e.what())
        .ThrowAsJavaScriptException();
    hasError = true;
    return Napi::Boolean::New(env, false);
  }
}

// Structure to hold work data for async AddWebm operation
struct AddWebmWorkData {
  Recorder *recorder = nullptr;
  std::vector<uint8_t> blob_data;
  Napi::ThreadSafeFunction tsfn;
  std::unique_ptr<Napi::Promise::Deferred> deferred;
  int frames_added = 0;
  bool success = false;
  std::string error_message;
};

// Callback function to be called from JavaScript thread via ThreadSafeFunction
static void CallJsCallback(Napi::Env env, Napi::Function jsCallback, AddWebmWorkData *workData) {
  if (!workData) {
    return;
  }

  // Resolve or reject the promise
  if (workData->success) {
    workData->deferred->Resolve(Napi::Boolean::New(env, workData->frames_added > 0 && !workData->recorder->getHasError()));
  } else {
    workData->deferred->Reject(Napi::Error::New(env, workData->error_message).Value());
  }

  // Release the ThreadSafeFunction - this will trigger the finalizer to delete workData
  workData->tsfn.Release();
}

// Worker thread function to process WebM data
static void ProcessWebmWorker(AddWebmWorkData *workData) {
  try {
    std::error_code ec;

    // Create a temporary memory buffer for the WebM data
    av::FormatContext input_ctx;

    // Create AVIOContext for memory buffer
    AVIOContext *avio_ctx = nullptr;
    uint8_t *avio_ctx_buffer = nullptr;
    size_t avio_ctx_buffer_size = 4096;

    struct buffer_data {
      const uint8_t *ptr;
      size_t size;
    } bd = {workData->blob_data.data(), workData->blob_data.size()};

    // Allocate buffer for AVIO
    avio_ctx_buffer = static_cast<uint8_t *>(av_malloc(avio_ctx_buffer_size));
    if (!avio_ctx_buffer) {
      workData->error_message = "Failed to allocate memory for AVIO buffer";
      workData->success = false;
      workData->tsfn.BlockingCall(workData, CallJsCallback);
      return;
    }

    // Create AVIO context
    avio_ctx = avio_alloc_context(
        avio_ctx_buffer, avio_ctx_buffer_size, 0, &bd,
        [](void *opaque, uint8_t *buf, int buf_size) -> int {
          auto *bd = static_cast<buffer_data *>(opaque);
          buf_size = FFMIN(buf_size, static_cast<int>(bd->size));
          if (!buf_size)
            return AVERROR_EOF;
          memcpy(buf, bd->ptr, buf_size);
          bd->ptr += buf_size;
          bd->size -= buf_size;
          return buf_size;
        },
        nullptr, nullptr);

    if (!avio_ctx) {
      av_freep(&avio_ctx_buffer);
      workData->error_message = "Failed to create AVIO context";
      workData->success = false;
      workData->tsfn.BlockingCall(workData, CallJsCallback);
      return;
    }

    // Set up format context with memory buffer
    input_ctx.raw()->pb = avio_ctx;
    input_ctx.raw()->flags |= AVFMT_FLAG_CUSTOM_IO;

    // Open input context
    input_ctx.openInput("", ec);
    if (ec) {
      av_freep(&avio_ctx->buffer);
      avio_context_free(&avio_ctx);
      workData->error_message = std::string("Failed to open WebM input: ") + ec.message();
      workData->success = false;
      workData->tsfn.BlockingCall(workData, CallJsCallback);
      return;
    }

    // Find stream information
    input_ctx.findStreamInfo(ec);
    if (ec) {
      av_freep(&avio_ctx->buffer);
      avio_context_free(&avio_ctx);
      workData->error_message = std::string("Failed to find stream info: ") + ec.message();
      workData->success = false;
      workData->tsfn.BlockingCall(workData, CallJsCallback);
      return;
    }

    // Find video stream
    int video_stream_index = -1;
    av::Stream video_stream;
    for (size_t i = 0; i < input_ctx.streamsCount(); ++i) {
      auto stream = input_ctx.stream(i);
      if (stream.mediaType() == AVMEDIA_TYPE_VIDEO) {
        video_stream_index = i;
        video_stream = stream;
        break;
      }
    }

    if (video_stream_index == -1) {
      av_freep(&avio_ctx->buffer);
      avio_context_free(&avio_ctx);
      workData->error_message = "No video stream found in WebM blob";
      workData->success = false;
      workData->tsfn.BlockingCall(workData, CallJsCallback);
      return;
    }

    // Check if it's VP8 or VP9
    auto codec_id = video_stream.codecParameters().codecId();
    if (codec_id != AV_CODEC_ID_VP8 && codec_id != AV_CODEC_ID_VP9) {
      av_freep(&avio_ctx->buffer);
      avio_context_free(&avio_ctx);
      workData->error_message = "WebM blob must contain VP8 or VP9 video";
      workData->success = false;
      workData->tsfn.BlockingCall(workData, CallJsCallback);
      return;
    }

    // Create decoder context
    av::VideoDecoderContext decoder(video_stream);
    decoder.setRefCountedFrames(true);

    // Open decoder
    decoder.open(av::Codec(), ec);
    if (ec) {
      av_freep(&avio_ctx->buffer);
      avio_context_free(&avio_ctx);
      workData->error_message = std::string("Failed to open decoder: ") + ec.message();
      workData->success = false;
      workData->tsfn.BlockingCall(workData, CallJsCallback);
      return;
    }

    // Process all packets in the WebM blob
    int frames_added = 0;
    while (true) {
      av::Packet packet = input_ctx.readPacket(ec);
      if (ec) {
        if (ec.value() == AVERROR_EOF) {
          // Normal end of file
          break;
        }
        // Log warning but continue processing
        av_log(nullptr, AV_LOG_WARNING, "Error reading packet: %s\n", ec.message().c_str());
        continue;
      }

      if (packet.isNull()) {
        break;
      }

      // Only process video stream packets
      if (packet.streamIndex() != video_stream_index) {
        continue;
      }

      // Decode packet to frame
      av::VideoFrame frame = decoder.decode(packet, ec);
      if (ec) {
        av_log(nullptr, AV_LOG_WARNING, "Decoding error: %s\n", ec.message().c_str());
        continue;
      }

      if (frame) {
        workData->recorder->push(frame.clone());
        frames_added++;
      }
    }

    // Flush decoder to get any remaining frames
    av::VideoFrame frame = decoder.decode(av::Packet(), ec);
    while (frame && !ec) {
      workData->recorder->push(frame.clone());
      frames_added++;
      frame = decoder.decode(av::Packet(), ec);
    }

    // Clean up
    av_freep(&avio_ctx->buffer);
    avio_context_free(&avio_ctx);
    input_ctx.close();

    workData->frames_added = frames_added;
    workData->success = true;
    workData->tsfn.BlockingCall(workData, CallJsCallback);

  } catch (const std::exception &e) {
    workData->error_message = std::string("Failed to process WebM blob: ") + e.what();
    workData->success = false;
    workData->recorder->setHasError(true);
    workData->tsfn.BlockingCall(workData, CallJsCallback);
  }
}

/**
 * Add WebM blob frames to the video (async version)
 * @param info JavaScript call info containing WebM blob buffer
 * @return Promise that resolves to Boolean indicating success
 */
Napi::Value Recorder::AddWebm(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (hasError || done) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(Napi::Error::New(env, "Recorder has error or is done").Value());
    return deferred.Promise();
  }
  if (info.Length() < 1) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(Napi::TypeError::New(env, "Expected 1 argument: WebM blob buffer").Value());
    return deferred.Promise();
  }
  if (!info[0].IsBuffer()) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(Napi::TypeError::New(env, "First argument must be a buffer").Value());
    return deferred.Promise();
  }

  auto blob_buffer = info[0].As<Napi::Buffer<uint8_t>>();
  if (blob_buffer.ByteLength() == 0) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(Napi::RangeError::New(env, "WebM blob buffer is empty").Value());
    return deferred.Promise();
  }

  // Create work data structure
  auto workData = new AddWebmWorkData();
  workData->recorder = this;
  workData->blob_data.assign(blob_buffer.Data(), blob_buffer.Data() + blob_buffer.ByteLength());
  workData->deferred = std::make_unique<Napi::Promise::Deferred>(env);

  // Create ThreadSafeFunction
  workData->tsfn = Napi::ThreadSafeFunction::New(
      env,
      Napi::Function::New(env, [](const Napi::CallbackInfo &info) {
        // This function is not used, but required by ThreadSafeFunction
        return info.Env().Undefined();
      }),
      "AddWebm",
      0, // Unlimited queue
      1, // Initial thread count
      workData,
      [](Napi::Env, void *finalizeData, AddWebmWorkData *context) {
        // Finalizer - cleanup workData when ThreadSafeFunction is released
        // This is called when Release() is called or when the TSFN is garbage collected
        if (context) {
          delete context;
        }
      },
      (void *)nullptr);

  // Start worker thread
  std::thread workerThread(ProcessWebmWorker, workData);
  workerThread.detach();

  return workData->deferred->Promise();
}

/**
 * Get the number of frames that were dropped due to buffer overflow
 * @param info JavaScript call info
 * @return Number of dropped frames
 */
Napi::Value Recorder::FramesAdded(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  return Napi::Number::New(env, count);
}

av::VideoRescaler *Recorder::get_rescaler(av::VideoFrame &frame,
                                          av::VideoEncoderContext &encoder_ctx) {
  // 使用缓存的rescaler以提高性能
  auto cache_key = std::make_tuple(frame.width(), frame.height(), frame.pixelFormat());

  av::VideoRescaler *frame_rescaler = nullptr;
  {
    std::lock_guard<std::mutex> lock(mtx_rescaler_cache);
    auto it = rescaler_cache.find(cache_key);
    if (it == rescaler_cache.end()) {
      // 创建新的rescaler并缓存
      av_log(nullptr, AV_LOG_DEBUG, "Creating new rescaler for format: %dx%d %s -> %dx%d %s\n",
             frame.width(), frame.height(), av_get_pix_fmt_name(frame.pixelFormat()),
             encoder.width(), encoder.height(), av_get_pix_fmt_name(encoder.pixelFormat()));

      auto result = rescaler_cache.emplace(
          cache_key, av::VideoRescaler(encoder.width(), encoder.height(), encoder.pixelFormat(),
                                       frame.width(), frame.height(), frame.pixelFormat()));
      frame_rescaler = &result.first->second;
    } else {
      frame_rescaler = &it->second;
    }
  }
  return frame_rescaler;
}

/**
 * Process frames in a background thread
 * This function runs continuously until done flag is set
 */
void Recorder::process_frames() {
  std::error_code ec;
  auto flushEncoder = false;
  av::VideoFrame frame;
  int64_t duration = 0;
  try {
    while (!flushEncoder) {
      auto got = pop(frame);
      if (!got) {
        flushEncoder = true;
      } else {
        av::VideoRescaler *frame_rescaler = get_rescaler(frame, encoder);
        frame = frame_rescaler->rescale(frame, ec);
        if (ec) {
          av_log(nullptr, AV_LOG_ERROR, "Can't rescale frame: %s\n", ec.message().c_str());
          hasError = true;
          return;
        }
      }
      if (got) {
        frame.setStreamIndex(0);
        frame.setPts({count++, av::Rational(1, this->fps)});
        // duration = frame.raw()->duration;
        // if (duration > 0) {
        //   frame.raw()->duration = duration;
        //   count+= duration;
        // }
      }
      bool encodingComplete = false;
      while (!encodingComplete) {
        av::Packet opkt = got ? encoder.encode(frame, ec) : encoder.encode(ec);
        if (ec) {
          av_log(nullptr, AV_LOG_ERROR, "Encoding error: %s\n", ec.message().c_str());
          hasError = true;
          return;
        } else if (!opkt) {
          encodingComplete = true;
          continue;
        }
        opkt.setStreamIndex(0);
        octx.writePacket(opkt, ec);
        if (ec) {
          if (ec.value() == AVERROR(EAGAIN)) {
            continue;
          }
          av_log(nullptr, AV_LOG_ERROR, "Error writing packet: %s\n", ec.message().c_str());
          hasError = true;
          return;
        }
        if (!flushEncoder) {
          encodingComplete = true;
        }
      }

      // Flush only if 2 seconds have passed since last flush, or if we're flushing the encoder
      auto now = std::chrono::steady_clock::now();
      auto time_since_flush = std::chrono::duration_cast<std::chrono::seconds>(now - last_flush_time);
      if (flushEncoder || time_since_flush >= FLUSH_INTERVAL) {
        octx.flush();
        last_flush_time = now;
      }
    }
  } catch (const std::exception &e) {
    av_log(nullptr, AV_LOG_ERROR, "Exception in process_frames: %s\n", e.what());
    hasError = true;
  }
}

/**
 * Push a frame to the processing queue
 * @param frame Video frame to add to the queue
 */
void Recorder::push(av::VideoFrame frame) {
  {
    std::unique_lock<std::mutex> lock(mtx_frames);
    if (frames.size() > MAX_BUF_FRAMES) {
      cv_queue.wait(lock, [this] { return frames.size() < MAX_BUF_FRAMES || done; });
      if (done) {
        return;
      }
    }
    frames.emplace_back(std::move(frame));
  }
  cv_frames.notify_one();
}

/**
 * Pop a frame from the processing queue
 * @param frame Output parameter to receive the popped frame
 * @return true if a frame was retrieved, false if queue is empty and done
 */
bool Recorder::pop(av::VideoFrame &frame) {
  std::unique_lock<std::mutex> lock(mtx_frames);
  if (frames.empty()) {
    octx.flush();
    cv_frames.wait(lock, [this] { return !frames.empty() || done; });
    if (frames.empty()) {
      return false;
    }
  }
  frame = std::move(frames.front());
  frames.pop_front();
  cv_queue.notify_one();
  return true;
}

/**
 * Clear all frames from the processing queue
 */
void Recorder::drop_frames() {
  std::unique_lock<std::mutex> lock(mtx_frames);
  frames.clear();
}

/**
 * Stop the processing thread and clean up resources
 */
void Recorder::stop_thread() {
  // Only proceed if output context is open
  if (octx.isOpened()) {
    // Signal processing thread to stop
    {
      std::unique_lock<std::mutex> lock(mtx_frames);
      done = true;
    }
    cv_frames.notify_one();
    cv_queue.notify_one();

    // Wait for thread to finish
    if (thrd.joinable()) {
      thrd.join();
    }

    // Write trailer and close output
    std::error_code ec;
    octx.writeTrailer(ec);
    if (ec) {
      av_log(nullptr, AV_LOG_WARNING, "Error writing trailer: %s\n", ec.message().c_str());
    }

    octx.close();
    owriter.reset();
  }

  // Clear any remaining frames
  drop_frames();
}
