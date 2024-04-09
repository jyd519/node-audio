#include "addon_api.h"

#include <stdlib.h>

#include <algorithm>
#include <string>
#include <vector>
#include <sstream>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/log.h>
}

#include "napi_help.h"
#include "ff_help.h"
#include "addon_api.h"

#include "screen_capturer.h"
#include "combine.h"

#include <memory>

// Input: buffer/filename, "video"/"audio" (default)
// Output: {status, duration}
napi_value get_audio_duration(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_status res;
  int r;
  size_t len;
  std::string in, out;
  napi_value result = nullptr;
  int duration = 0;
  bool is_buffer;
  void *data;
  enum AVMediaType media = AVMEDIA_TYPE_AUDIO;
  size_t length;

  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 1) {
    napi_throw_error(env, "EINVAL", "Expected string: input file path");
    return nullptr;
  }

  // buffer or filename
  res = napi_is_buffer(env, argv[0], &is_buffer);
  if (res != napi_ok || !is_buffer) {
    if ((res = get_utf8_string(env, argv[0], &in)) != napi_ok) {
      napi_throw_error(env, "EINVAL", "Something went wrong.");
      return nullptr;
    }
  } else {
    res = napi_get_buffer_info(env, argv[0], &data, &length);
    if (res != napi_ok) {
      napi_throw_error(env, "EINVAL", "invalid buffer");
      return nullptr;
    }
  }

  if (argc > 1) {
    std::string type;
    get_utf8_string(env, argv[1], &type);
    if (type == "video") {
      media = AVMEDIA_TYPE_VIDEO;
    }
  }

  if (is_buffer) {
    r = ff_get_av_duration_buffer((const uint8_t *)data, length, media, &duration);
  } else {
    r = ff_get_av_duration(in.c_str(), media, &duration);
  }

  napi_value v_retcode, v_duration;
  if ((res = napi_create_int32(env, r, &v_retcode)) != napi_ok)
    goto end;

  if ((res = napi_create_int32(env, duration, &v_duration)) != napi_ok)
    goto end;

  if ((res = napi_create_object(env, &result)) != napi_ok)
    goto end;

  if ((res = napi_set_named_property(env, result, "status", v_retcode)) != napi_ok)
    goto end;

  if ((res = napi_set_named_property(env, result, "duration", v_duration)) != napi_ok)
    goto end;

end:
  if (res != napi_ok) {
    napi_throw_error(env, "EINVAL", "Something went wrong.");
    return nullptr;
  }

  return result;
}

napi_status get_property(napi_env env, napi_value object, const std::string &key,
                         napi_value *retval) {
  napi_value result;
  auto status = napi_get_named_property(env, object, key.c_str(), &result);
  if (status != napi_ok) {
    return status;
  }
  return status;
}

napi_status get_property_int(napi_env env, napi_value object, const std::string &key,
                             int64_t *retval) {
  napi_value result;
  auto status = napi_get_named_property(env, object, key.c_str(), &result);
  if (status != napi_ok) {
    return status;
  }
  return napi_get_value_int64(env, result, retval);
}

// Input: filepath, options { start , duration }
// Output: result { max_volume, mean_volume, status}
napi_value get_audio_volume_info(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_status res;
  int r;
  size_t len;
  std::string in, out;
  int64_t start = -1, duration = -1;
  napi_value result = nullptr;
  float max_volume, mean_volume;
  bool is_buffer;
  void *data;
  size_t length;

  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 1) {
    napi_throw_error(env, "EINVAL", "Expected string: input file path");
    return nullptr;
  }

  // arg 1:  buffer or filename
  res = napi_is_buffer(env, argv[0], &is_buffer);
  if (res != napi_ok || !is_buffer) {
    if ((res = get_utf8_string(env, argv[0], &in)) != napi_ok) {
      napi_throw_error(env, "EINVAL", "Something went wrong.");
      return nullptr;
    }
  } else {
    res = napi_get_buffer_info(env, argv[0], &data, &length);
    if (res != napi_ok) {
      napi_throw_error(env, "EINVAL", "invalid buffer");
      return nullptr;
    }
  }

  // arg2 ?
  if (argc > 1) {
    get_property_int(env, argv[1], "start", &start);
    get_property_int(env, argv[1], "duration", &duration);
  }

  // get volume info
  if (is_buffer) {
    r = ff_get_audio_volume_buffer((const uint8_t *)data, length, start, duration, &max_volume,
                                   &mean_volume);
  } else {
    r = ff_get_audio_volume(in.c_str(), start, duration, &max_volume, &mean_volume);
  }

  napi_value v_retcode, v_max, v_mean;
  if ((res = napi_create_int32(env, r, &v_retcode)) != napi_ok)
    goto end;

  if ((res = napi_create_double(env, max_volume, &v_max)) != napi_ok)
    goto end;
  if ((res = napi_create_double(env, mean_volume, &v_mean)) != napi_ok)
    goto end;

  if ((res = napi_create_object(env, &result)) != napi_ok)
    goto end;

  if ((res = napi_set_named_property(env, result, "status", v_retcode)) != napi_ok)
    goto end;

  if ((res = napi_set_named_property(env, result, "max_volume", v_max)) != napi_ok)
    goto end;
  if ((res = napi_set_named_property(env, result, "mean_volume", v_mean)) != napi_ok)
    goto end;

end:
  if (res != napi_ok) {
    napi_throw_error(env, "EINVAL", "Something went wrong.");
    return nullptr;
  }

  return result;
}

// Input: args
// Output: result { status, data}
napi_value probe(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_status res;
  std::vector<napi_value> argv;

  // Get the argument count
  napi_get_cb_info(env, info, &argc, NULL, NULL, NULL);
  argv.resize(argc);
  res = napi_get_cb_info(env, info, &argc, argv.data(), NULL, NULL);
  if (argc < 1) {
    napi_throw_error(env, "EINVAL", "invalid arguments");
    return nullptr;
  }

  std::vector<const char *> args;
  std::vector<std::string> argsOwner;
  {
    argsOwner.push_back("ffprobe");
    for (size_t i = 0; i < argc; i++) {
      std::string s;
      res = get_utf8_string(env, argv[i], &s);
      if (res != napi_ok) {
        napi_throw_error(env, "EINVAL", "argument must be string");
        return nullptr;
      }
      argsOwner.push_back(s);
    }

    std::for_each(argsOwner.begin(), argsOwner.end(),
                  [&args](const std::string &s) { args.push_back(s.c_str()); });
    args.push_back(nullptr);
  }

  std::string json;
  char *out = nullptr;
  int outsize;
  auto ret = ff_probe(args.size() - 1, args.data(), &out, &outsize);
  if (ret == 0) {
    json.assign(out, outsize);
    ff_free(out);
  }

  if (ret != 0) {
    std::stringstream ss;
    ss << "probe failed: " << ret;
    napi_throw_error(env, "EINVAL", ss.str().c_str());
    return nullptr;
  }

  // Parse the string into a JSON object
  napi_value result;
  ret = JSONParse(env, json, &result);
  if (ret != napi_ok) {
    napi_throw_error(env, "EINVAL", "Failed to parse JSON");
    return nullptr;
  }
  return result;
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
  for (int i = 0; i < input.Length(); i++) {
    auto item = input.Get(i).As<Napi::String>();
    inputs.push_back(item.Utf8Value());
  }

  int ret = ff_combine(inputs, output.Utf8Value());
  return Napi::Number::New(env, ret);
}
