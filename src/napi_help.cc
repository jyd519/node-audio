#include "napi_help.h"

napi_status get_utf8_string(napi_env env, napi_value str, std::string *s) {
  size_t len;
  napi_status res;

  if (s == NULL) {
    return napi_invalid_arg;
  }

  res = napi_get_value_string_utf8(env, str, NULL, 0, &len);
  if (res != napi_ok) {
    return res;
  }

  s->resize(len);
  res = napi_get_value_string_utf8(env, str, const_cast<char *>(s->data()),
                                   len + 1, &len);
  if (res != napi_ok) {
    return napi_generic_failure;
  }

  return napi_ok;
}


int *getArgs(napi_env env, napi_callback_info info) {
  napi_value argv[2];
  size_t argc = 2;
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  int *out = (int *)malloc(sizeof(int) * argc);
  for (int i = 0; i < (int)argc; i++) {
    napi_get_value_int32(env, argv[i], &out[i]);
  }
  return out;
}

napi_value toValue(napi_env env, int value) {
  napi_value nvalue = 0;
  napi_create_int32(env, value, &nvalue);
  return nvalue;
}

napi_status JSONParse(napi_env env, const std::string& text, napi_value* result) {
    napi_status status;

    napi_value global;
    status = napi_get_global(env, &global);
    if (status != napi_ok) {
        return status;
    }

    // Get the JSON object from the global object
    napi_value json;
    status = napi_get_named_property(env, global, "JSON", &json);
    if (status != napi_ok) {
        return status;
    }

    // Parse the string into a JSON object using JSON.parse()
    napi_value parse_fn;
    status = napi_get_named_property(env, json, "parse", &parse_fn);
    if (status != napi_ok) {
        return status;
    }

    napi_value argv[1];
    status = napi_create_string_utf8(env, text.c_str(), text.length(), &argv[0]);
    if (status != napi_ok) {
        return status;
    }

    status = napi_call_function(env, json, parse_fn, 1, argv, result);
    if (status != napi_ok) {
        return status;
    }

    return napi_ok;
}
