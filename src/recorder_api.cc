#include "recorder_api.h"
#include "addon_api.h"
#include "timestamp.h"
#include "enc_writer.h"

// Maximum number of frames to buffer before dropping frames
constexpr int MAX_BUF_FRAMES = 30;
// Number of frames to drop when buffer overflows
constexpr int FRAMES_TO_DROP = 5;
// Frame pool: get a frame from the pool or create a new one
constexpr int FRAME_POOL_MAX = 10;

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
          InstanceMethod<&Recorder::Close>(
              "Close", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
          InstanceMethod<&Recorder::FramesDropped>(
              "FramesDropped",
              static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
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
  int gop = 40;
  int64_t bitrate = 8000 * 1000;
  std::string format = "mp4";
  std::string movflags = "frag_keyframe+empty_moov";
  std::string encoder_opt = "profile=main;rc_mode=bitrate;allow_skip_frames=true";
  std::string comment;
  std::string meta;

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
  if (auto v = opts.Get("meta"); v.IsString()) {
    meta = v.ToString();
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
    encoder.setGopSize(gop);

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

    // Parse metadata string if provided
    if (!meta.empty()) {
      metaDic.parseString(meta, "=", ";", 0, ec);
      if (ec) {
        av_log(nullptr, AV_LOG_WARNING, "Can't parse meta options: %s\n", ec.message().c_str());
      }
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
      frames_dropped = 0;
    }

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
Recorder::~Recorder() { stop_thread(); }

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
    Napi::TypeError::New(env, "Expected 3 arguments: buffer, width, height")
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
  try {
    // Use frame pool
    av::VideoFrame frame = get_frame_from_pool();
    // Re-initialize frame with new data
    frame = av::VideoFrame(static_cast<const uint8_t *>(buf.Data()), buf.ByteLength(),
                           AV_PIX_FMT_RGBA, width_im, height_im);
    push(std::move(frame));
    return Napi::Boolean::New(env, !hasError);
  } catch (const std::exception &e) {
    Napi::Error::New(env, std::string("Failed to add image: ") + e.what())
        .ThrowAsJavaScriptException();
    hasError = true;
    return Napi::Boolean::New(env, false);
  }
}

/**
 * Get the number of frames that were dropped due to buffer overflow
 * @param info JavaScript call info
 * @return Number of dropped frames
 */
Napi::Value Recorder::FramesDropped(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  return Napi::Number::New(env, frames_dropped);
}

/**
 * Process frames in a background thread
 * This function runs continuously until done flag is set
 */
void Recorder::process_frames() {
  std::error_code ec;
  auto flushEncoder = false;
  av::VideoFrame frame;

  try {
    while (!flushEncoder) {
      auto got = pop(frame);
      if (!got) {
        flushEncoder = true;
      } else {
        frame = rescaler.rescale(frame, ec);
        if (ec) {
          av_log(nullptr, AV_LOG_ERROR, "Can't rescale frame: %s\n", ec.message().c_str());
          hasError = true;
          return;
        }
      }
      if (frame) {
        frame.setStreamIndex(0);
        frame.setPts({count++, av::Rational(1, this->fps)});
      }
      bool encodingComplete = false;
      while (!encodingComplete) {
        av::Packet opkt = frame ? encoder.encode(frame, ec) : encoder.encode(ec);
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
      // Return frame to pool after use
      if (!flushEncoder) {
        return_frame_to_pool(std::make_unique<av::VideoFrame>(std::move(frame)));
        frame = av::VideoFrame();
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
      av_log(nullptr, AV_LOG_WARNING, "Buffer overflow, discarding %d frames\n", FRAMES_TO_DROP);
      frames_dropped += FRAMES_TO_DROP;
      // Return dropped frames to pool
      for (int i = 0; i < FRAMES_TO_DROP && !frames.empty(); ++i) {
        auto &f = frames.front();
        return_frame_to_pool(std::make_unique<av::VideoFrame>(std::move(f)));
        frames.pop_front();
      }
      return;
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
  cv_frames.wait(lock, [this] { return !frames.empty() || done; });
  if (frames.empty() && done) {
    return false;
  }
  frame = std::move(frames.front());
  frames.pop_front();
  return true;
}

/**
 * Clear all frames from the processing queue
 */
void Recorder::drop_frames() {
  std::unique_lock<std::mutex> lock(mtx_frames);
  for (auto &f : frames) {
    return_frame_to_pool(std::make_unique<av::VideoFrame>(std::move(f)));
  }
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

// Frame pool: get a frame from the pool or create a new one
av::VideoFrame Recorder::get_frame_from_pool() {
  std::lock_guard<std::mutex> lock(mtx_pool);
  if (!frame_pool.empty()) {
    auto frame_ptr = std::move(frame_pool.back());
    frame_pool.pop_back();
    return std::move(*frame_ptr);
  }
  return av::VideoFrame();
}

void Recorder::return_frame_to_pool(std::unique_ptr<av::VideoFrame> frame) {
  std::lock_guard<std::mutex> lock(mtx_pool);
  if (frame_pool.size() < FRAME_POOL_MAX) {
    frame_pool.push_back(std::move(frame));
  }
  // else let unique_ptr delete it
}
