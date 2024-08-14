#include "recorder_api.h"
#include "addon_api.h"

constexpr int MAX_BUF_FRAMES = 30;

static Napi::Value setFFmpegLoggingLevel(const Napi::CallbackInfo &info) {
  std::string level = info[0].ToString();
  av::set_logging_level(level);
  return Napi::Boolean::From(info.Env(), true);
}

Napi::Object Recorder::Init(Napi::Env env, Napi::Object exports) {
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

  Napi::FunctionReference *constructor = new Napi::FunctionReference();

  *constructor = Napi::Persistent(func);
  auto pInstance = env.GetInstanceData<InstanceData>();
  pInstance->recorder_ctor = constructor;

  av::init();
  av::set_logging_level("warning");

  exports.Set("Recorder", func);
  exports.Set("SetFFmpegLoggingLevel", Napi::Function::New(env, setFFmpegLoggingLevel));
  return exports;
}

Recorder::Recorder(const Napi::CallbackInfo &info) : Napi::ObjectWrap<Recorder>(info) {
  auto uri = info[0].ToString().Utf8Value();
  auto opts = info[1].ToObject();
  int gop = 40;
  int64_t bitrate = 8000 * 1000;
  std::string format = "mp4";
  std::string movflags = "frag_keyframe+empty_moov";
  std::string encoder_opt = "profile=main;rc_mode=bitrate;allow_skip_frames=true";
  std::string comment;
  std::string meta;
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

  std::error_code ec;
  ofrmt.setFormat(format, uri);
  encoder.setCodec(av::findEncodingCodec(AV_CODEC_ID_H264), true);
  octx.setFormat(ofrmt);
  octx.setSocketTimeout(5000);

  encoder.setWidth(this->width);
  encoder.setHeight(this->height);
  encoder.setPixelFormat(AV_PIX_FMT_YUV420P);
  encoder.setTimeBase(av::Rational{1, 1000});
  encoder.setBitRate(bitrate);
  encoder.setGopSize(gop);
  if (octx.outputFormat().isFlags(AVFMT_GLOBALHEADER)) {
    encoder.addFlags(AV_CODEC_FLAG_GLOBAL_HEADER);
  }

  av::Dictionary options;
  options.parseString(encoder_opt, "=", ";", 0, ec);
  if (ec) {
    std::cerr << "Can't parse encoder options: " << ec << "\n";
  }
  encoder.open(options, av::Codec(), ec);
  if (ec) {
    std::cerr << "Can't open encoder\n";
    hasError = true;
    return;
  }

  octx.openOutput(uri, ec);
  if (ec) {
    std::cerr << "Can't open output\n";
    hasError = true;
    return;
  }

  av::Stream ost = octx.addStream(encoder);
  ost.setFrameRate(vst.frameRate());

  rescaler = av::VideoRescaler(encoder.width(), encoder.height(), encoder.pixelFormat());
  octx.dump();

  av::Dictionary metaDic = av::Dictionary(octx.raw()->metadata, false);
  if (!meta.empty()) {
    metaDic.parseString(meta, "=", ";", 0, ec);
    if (ec) {
      std::cerr << "Can't parse meta options: " << ec << "\n";
    }
  }
  if (!metaDic.get("creation_time")) {
    time_t rawtime{};
    struct tm timeinfo{};
    char buffer[80] = {0};
    time(&rawtime);
    gmtime_r(&rawtime, &timeinfo);
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    metaDic.set("creation_time", buffer);
  }
  if (!comment.empty()) {
    metaDic.set("comment", comment);
  }
  if (metaDic.isOwning()) {
    octx.raw()->metadata = metaDic.release();
  }

  options.set("movflags", movflags);
  octx.writeHeader(options);
  octx.flush();

  {
    std::unique_lock<std::mutex> lock(mtx_frames);
    done = false;
    frames.clear();
    frames_dropped = 0;
  }

  thrd = std::thread(&Recorder::process_frames, this);
}

Recorder::~Recorder() { stop_thread(); }

Napi::Value Recorder::Close(const Napi::CallbackInfo &info) {
  stop_thread();
  return Napi::Boolean::New(info.Env(), true);
}

Napi::Value Recorder::AddImage(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (hasError || done) {
    return Napi::Boolean::New(env, false);
  }

  if (info.Length() < 3) {
    Napi::Error::New(info.Env(), "Expected 3 arguments").ThrowAsJavaScriptException();
  }

  if (!info[0].IsBuffer()) {
    Napi::Error::New(env, "Invalid argument").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  auto buf = info[0].As<Napi::Buffer<uint8_t>>();
  auto width_im = info[1].ToNumber().Int32Value();
  auto height_im = info[2].ToNumber().Int32Value();

  std::error_code ec;
  av::VideoFrame frame = av::VideoFrame((const uint8_t *)buf.Data(), buf.ByteLength(),
                                        AV_PIX_FMT_RGBA, width_im, height_im);

  push(frame);
  return Napi::Boolean::New(env, !hasError);
}

Napi::Value Recorder::FramesDropped(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  return Napi::Number::From(env, frames_dropped);
}

void Recorder::process_frames() {
  std::error_code ec;
  auto flushEncoder = false;
  av::VideoFrame frame;
  while (!flushEncoder) {
    auto got = pop(frame);
    if (!got) {
      flushEncoder = true;
    } else {
      frame = rescaler.rescale(frame, ec);
      if (ec) {
        hasError = true;
        std::cerr << "Can't rescale frame: " << ec << ", " << ec.message() << std::endl;
      }
    }

    frame.setPts(av::Timestamp{count++, av::Rational(1, this->fps)});
    frame.setStreamIndex(0);

    do {
      // Encode
      av::Packet opkt = frame ? encoder.encode(frame, ec) : encoder.encode(ec);
      if (ec) {
        std::cerr << "Encoding error: " << ec << std::endl;
      } else if (!opkt) {
        break;
      }

      // Only one output stream
      opkt.setStreamIndex(0);

      opkt.setDuration(1, av::Rational(1, this->fps));
      octx.writePacket(opkt, ec);
      if (ec) {
        if (ec.value() == AVERROR(EAGAIN)) {
          continue;
        }
        std::cerr << "Error write packet: " << ec << ", " << ec.message() << std::endl;
        hasError = true;
        return;
      }
      if (flushEncoder) {
        break;
      }
    } while (flushEncoder);
  }
}

void Recorder::push(av::VideoFrame frame) {
  {
    std::unique_lock<std::mutex> g(mtx_frames);
    if (frames.size() > MAX_BUF_FRAMES) {
      av_log(nullptr, AV_LOG_WARNING, "Buffer overflow, discards 5 frames\n");
      frames_dropped += 5;
      frames.erase(frames.begin(), frames.begin() + 5);
      return;
    } else {
      frames.emplace_back(frame);
    }
  }
  cv_frames.notify_one();
}

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

void Recorder::drop_frames() {
  std::unique_lock<std::mutex> lock(mtx_frames);
  frames.clear();
}

void Recorder::stop_thread() {
  if (octx.isOpened()) {
    {
      std::unique_lock<std::mutex> lock(mtx_frames);
      done = true;
    }
    cv_frames.notify_one();
    if (thrd.joinable())
      thrd.join();
    std::error_code ec;
    octx.writeTrailer(ec);
    octx.close();
  }
  drop_frames();
}
