#include "napi.h"
#ifdef ENABLE_FFMPEG
#include "addon_api.h"

#include <stdlib.h>

#include <string>
#include <vector>
#include <sstream>
#include <memory>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/log.h>
}

#include "ff_help.h"
#include "addon_api.h"
#include "napi_help.h"
#include "screen_capturer.h"
#include "combine.h"
#include "fixwebm.h"


// Input: buffer/filename, "video"/"audio" (default)
// Output: {status, duration}
Napi::Value get_audio_duration(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1) {
    Napi::Error::New(env, "Expected string: input file path").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string in;
  std::string password;
  bool is_buffer = false;
  void* data = nullptr;
  size_t length = 0;
  enum AVMediaType media = AVMEDIA_TYPE_AUDIO;
  int duration = 0;
  int r = 0;

  // Check if first argument is buffer or filename
  if (info[0].IsBuffer()) {
    is_buffer = true;
    Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
    data = buffer.Data();
    length = buffer.Length();
  } else if (info[0].IsString()) {
    in = info[0].As<Napi::String>().Utf8Value();
  } else {
    Napi::Error::New(env, "First argument must be a buffer or string").ThrowAsJavaScriptException();
    return env.Null();
  }

  // Check second argument for media type
  if (info.Length() > 1) {
    if (info[1].IsString()) {
      std::string type = info[1].As<Napi::String>().Utf8Value();
      if (type == "video") {
        media = AVMEDIA_TYPE_VIDEO;
      }
    } else if(info[1].IsObject()) {
      Napi::Object opts = info[1].As<Napi::Object>();
      if (opts.Has("media")) {
        std::string type = opts.Get("media").As<Napi::String>().Utf8Value();
        if (type == "video") {
          media = AVMEDIA_TYPE_VIDEO;
        }
      }
      if (opts.Has("password")) {
        password = opts.Get("password").As<Napi::String>().Utf8Value();
      }
    } else {
      Napi::Error::New(env, "Second argument must be a string or object").ThrowAsJavaScriptException();
      return env.Null();
    }
  }

  // Get duration based on input type
  if (is_buffer) {
    r = ff_get_av_duration_buffer(static_cast<const uint8_t*>(data), length, media, &duration);
  } else {
    r = ff_get_av_duration(in.c_str(), password.c_str(), media, &duration);
  }

  // Create result object
  Napi::Object result = Napi::Object::New(env);
  result.Set("status", Napi::Number::New(env, r));
  result.Set("duration", Napi::Number::New(env, duration));

  return result;
}

Napi::Value get_audio_volume_info(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected string: input file path").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string in, password;
  bool isBuffer = false;
  void* data = nullptr;
  size_t length = 0;
  int64_t start = -1, duration = -1;
  float maxVolume, meanVolume;
  int r = 0;

  if (info[0].IsBuffer()) {
    isBuffer = true;
    Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
    data = buffer.Data();
    length = buffer.Length();
  } else if (info[0].IsString()) {
    in = info[0].As<Napi::String>().Utf8Value();
  } else {
    Napi::TypeError::New(env, "First argument must be a buffer or string").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() > 1 && info[1].IsObject()) {
    Napi::Object opts = info[1].As<Napi::Object>();
    if (opts.Has("start")) {
      start = opts.Get("start").As<Napi::Number>().Int64Value();
    }
    if (opts.Has("duration")) {
      duration = opts.Get("duration").As<Napi::Number>().Int64Value();
    }
    if (opts.Has("password")) {
      password = opts.Get("password").ToString();
    }
  }

  if (isBuffer) {
    r = ff_get_audio_volume_buffer(static_cast<const uint8_t*>(data), length, start, duration, &maxVolume, &meanVolume);
  } else {
    r = ff_get_audio_volume(in.c_str(), password.c_str(), start, duration, &maxVolume, &meanVolume);
  }

  Napi::Object result = Napi::Object::New(env);
  result.Set("status", Napi::Number::New(env, r));
  result.Set("max_volume", Napi::Number::New(env, maxVolume));
  result.Set("mean_volume", Napi::Number::New(env, meanVolume));

  return result;
}

Napi::Value probe(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Invalid arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::vector<std::string> argsOwner;
  std::vector<const char*> args;

  argsOwner.push_back("ffprobe");
  for (size_t i = 0; i < info.Length(); i++) {
    if (!info[i].IsString()) {
      Napi::TypeError::New(env, "Argument must be string").ThrowAsJavaScriptException();
      return env.Null();
    }
    argsOwner.push_back(info[i].As<Napi::String>().Utf8Value());
  }

  for (const auto& arg : argsOwner) {
    args.push_back(arg.c_str());
  }
  args.push_back(nullptr);

  std::string json;
  char* out = nullptr;
  int outsize;
  int ret = ff_probe(args.size() - 1, args.data(), &out, &outsize);
  if (ret == 0) {
    json.assign(out, outsize);
    ff_free(out);
  } else {
    std::stringstream ss;
    ss << "Probe failed: " << ret;
    Napi::TypeError::New(env, ss.str()).ThrowAsJavaScriptException();
    return env.Null();
  }

  napi_value result;
  auto n = JSONParse(env, json, &result);
  if (n != napi_ok) {
    Napi::TypeError::New(env, "Failed to parse JSON").ThrowAsJavaScriptException();
    return env.Null();
  }
  return Napi::Value::From(env, result);
}

Napi::Value record_screen(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }
  auto filepath = info[0].As<Napi::String>().Utf8Value();
  auto capturer = std::make_shared<ScreenCapturer>(filepath);
  if (info.Length() > 1) {
    auto opts = info[1].As<Napi::Object>();
    if (opts.Has("password")) {
      auto password = opts.Get("password").As<Napi::String>().Utf8Value();
      capturer->set_password(password);
      opts.Delete("password");
    }
    if (opts.Has("quality")) {
      capturer->set_quality(opts.Get("quality").As<Napi::Number>().Int32Value());
      opts.Delete("quality");
    }
    if (opts.Has("fps")) {
      capturer->set_fps(opts.Get("fps").As<Napi::Number>().Int32Value());
      opts.Delete("fps");
    }
    if (opts.Has("gop")) {
      capturer->set_gop(opts.Get("gop").As<Napi::Number>().Int32Value());
      opts.Delete("gop");
    }
    if (opts.Has("width") && opts.Has("height")) {
      capturer->set_size(opts.Get("width").As<Napi::Number>().Int32Value(),
                         opts.Get("height").As<Napi::Number>().Int32Value());
      opts.Delete("width");
      opts.Delete("height");
    }

    auto names = opts.GetPropertyNames().As<Napi::Array>();
    for (int i = 0; i < names.Length(); i++) {
      auto name = names.Get(i).ToString().Utf8Value();
      capturer->set_option(name, opts.Get(name).ToString().Utf8Value());
    }
  }

  capturer->start();

  Napi::Object result = Napi::Object::New(env);
  result.DefineProperty(
      Napi::PropertyDescriptor::Function("stop", [c = capturer](const Napi::CallbackInfo &info) {
        return Napi::Number::New(info.Env(), c->stop());
      }));
  result.DefineProperty(Napi::PropertyDescriptor::Function(
      "start", [c = capturer](const Napi::CallbackInfo &info) { c->start(); }));
  result.DefineProperty(Napi::PropertyDescriptor::Function(
      "pause", [c = capturer](const Napi::CallbackInfo &info) { c->pause(); }));
  result.AddFinalizer([c = capturer](Napi::Env env, int *ptr) { c->stop(); }, (int *)0);
  return result;
}

Napi::Value combine(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::vector<std::string> inputs;
  auto output = info[0].As<Napi::String>();
  auto input = info[1].As<Napi::Array>();
  for (unsigned int i = 0; i < input.Length(); i++) {
    auto item = input.Get(i).As<Napi::String>();
    inputs.push_back(item.Utf8Value());
  }

  int ret = ff_combine(inputs, output.Utf8Value());
  return Napi::Number::New(env, ret);
}

Napi::Value fixwebmfile(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto input = info[0].As<Napi::String>().Utf8Value();
  auto output = info[1].As<Napi::String>().Utf8Value();

  std::map<std::string, std::string> metadata;
  if (info.Length() > 2 && info[2].IsObject()) {
    auto metadataObj = info[2].As<Napi::Object>();
    auto properties = metadataObj.GetPropertyNames();
    for (uint32_t i = 0; i < properties.Length(); i++) {
      auto key = properties.Get(i).As<Napi::String>().Utf8Value();
      auto value = metadataObj.Get(key).As<Napi::String>().Utf8Value();
      metadata[key] = value;
    }
  }

  bool ret = remuxWebmFile(input, output, metadata);
  return Napi::Number::New(env, ret ? 0 : 1);
}

class FixWebmWorker : public Napi::AsyncWorker {
private:
  std::string input;
  std::string output;
  std::map<std::string, std::string> meta;
  bool result;

public:
  FixWebmWorker(const std::string &inputPath, const std::string &outputPath,
                std::map<std::string, std::string> meta, Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(deferred.Env()), input(inputPath), output(outputPath), result(false),
        meta(std::move(meta)), deferred(deferred) {}

  void Execute() override { result = remuxWebmFile(input, output, meta); }

  void OnOK() override { deferred.Resolve(Napi::Number::New(Env(), result ? 0 : 1)); }

  void OnError(const Napi::Error &e) override { deferred.Reject(e.Value()); }

private:
  Napi::Promise::Deferred deferred;
};

Napi::Value fixwebmfileAsync(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto input = info[0].As<Napi::String>().Utf8Value();
  auto output = info[1].As<Napi::String>().Utf8Value();

  std::map<std::string, std::string> metadata;
  if (info.Length() > 2 && info[2].IsObject()) {
    auto metadataObj = info[2].As<Napi::Object>();
    auto properties = metadataObj.GetPropertyNames();
    for (uint32_t i = 0; i < properties.Length(); i++) {
      auto key = properties.Get(i).As<Napi::String>().Utf8Value();
      auto value = metadataObj.Get(key).As<Napi::String>().Utf8Value();
      metadata[key] = value;
    }
  }

  auto deferred = Napi::Promise::Deferred::New(env);
  auto worker = new FixWebmWorker(input, output, std::move(metadata), deferred);
  worker->Queue();

  return deferred.Promise();
}
#endif
