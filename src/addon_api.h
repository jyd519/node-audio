#pragma once

#include <node_api.h>
#include <napi.h>

struct InstanceData {
    Napi::FunctionReference *recorder_ctor = nullptr;
};

#ifdef ENABLE_FFMPEG
Napi::Value get_audio_duration(const Napi::CallbackInfo& info);
Napi::Value get_audio_volume_info(const Napi::CallbackInfo& info);
Napi::Value probe(const Napi::CallbackInfo& info);

Napi::Value record_screen(const Napi::CallbackInfo& info);
Napi::Value combine(const Napi::CallbackInfo &info);
Napi::Value fixwebmfile(const Napi::CallbackInfo &info);
Napi::Value fixwebmfileAsync(const Napi::CallbackInfo &info);
Napi::Value getMetaTags(const Napi::CallbackInfo& info);
#endif
