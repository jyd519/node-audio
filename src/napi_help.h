#pragma once

#include <node_api.h>
#include <string>

napi_status get_utf8_string(napi_env env, napi_value str, std::string *s);

int *getArgs(napi_env env, napi_callback_info info);

napi_value toValue(napi_env env, int value);

napi_status JSONParse(napi_env env, const std::string& text, napi_value* result);

