#pragma once

#include <node_api.h>
#include <napi.h>

napi_value get_audio_duration(napi_env env, napi_callback_info info);
napi_value get_audio_volume_info(napi_env env, napi_callback_info info);
napi_value probe(napi_env env, napi_callback_info info);

Napi::Value record_screen(const Napi::CallbackInfo& info);
Napi::Value combine(const Napi::CallbackInfo &info);
