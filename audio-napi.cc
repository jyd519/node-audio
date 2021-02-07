#ifdef _WIN32
#include <atlbase.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <windows.h>
#endif

#include <math.h>
#include <node_api.h>
#include <stdlib.h>

#include <sstream>
#include <string>

#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <unistd.h>
#endif

#include "webm_muxer.h"

#define CHECK(expr)                                                         \
  {                                                                         \
    if ((expr) == 0) {                                                      \
      fprintf(stderr, "%s:%d: failed assertion `%s'\n", __FILE__, __LINE__, \
              #expr);                                                       \
      fflush(stderr);                                                       \
      abort();                                                              \
    }                                                                       \
  }

#ifdef __APPLE__
typedef enum {
  kAudioTypeUnknown = 0,
  kAudioTypeInput = 1,
  kAudioTypeOutput = 2,
  kAudioTypeSystemOutput = 3
} ASDeviceType;

#endif

#ifdef __APPLE__
AudioDeviceID GetDefaultInputDevice() {
  AudioDeviceID theAnswer = 0;
  UInt32 theSize = sizeof(AudioDeviceID);
  AudioObjectPropertyAddress theAddress = {
      kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMaster};

  OSStatus theError = AudioObjectGetPropertyData(
      kAudioObjectSystemObject, &theAddress, 0, NULL, &theSize, &theAnswer);
  // handle errors

  return theAnswer;
}

AudioDeviceID GetDefaultOutputDevice() {
  AudioDeviceID theAnswer = 0;
  UInt32 theSize = sizeof(AudioDeviceID);
  AudioObjectPropertyAddress theAddress = {
      kAudioHardwarePropertyDefaultOutputDevice,
      kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster};

  OSStatus theError = AudioObjectGetPropertyData(
      kAudioObjectSystemObject, &theAddress, 0, NULL, &theSize, &theAnswer);
  // handle errors
  return theAnswer;
}
#endif

#ifdef _WIN32
class ComInitializer {
 public:
  HRESULT hr;
  ComInitializer() { hr = CoInitializeEx(NULL, COINIT_MULTITHREADED); }
  ~ComInitializer() {
    if (SUCCEEDED(hr)) {
      CoUninitialize();
    }
  }
};

IAudioEndpointVolume *getVolume(int mic) {
  HRESULT hr;
  IMMDeviceEnumerator *enumerator = NULL;
  IAudioEndpointVolume *volume = NULL;
  IMMDevice *defaultDevice = NULL;

  hr =
      CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
                       __uuidof(IMMDeviceEnumerator), (LPVOID *)&enumerator);
  if (FAILED(hr)) {
    goto clean;
  }
  hr = enumerator->GetDefaultAudioEndpoint(mic ? eCapture : eRender, eConsole,
                                           &defaultDevice);
  if (FAILED(hr)) {
    goto clean;
  }
  hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume),
                               CLSCTX_INPROC_SERVER, NULL, (LPVOID *)&volume);
  if (FAILED(hr)) {
    goto clean;
  }

clean:
  if (enumerator) enumerator->Release();
  if (defaultDevice) defaultDevice->Release();
  return volume;
}
#endif

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

napi_value get(napi_env env, napi_callback_info info) {
  int *argv = getArgs(env, info);
  float volume = 0;

#ifdef _WIN32
  ComInitializer ole;
  CComPtr<IAudioEndpointVolume> tmp_volume = getVolume(argv[0]);
  if (tmp_volume == NULL) {
    return toValue(env, -1);
  }

  tmp_volume->GetMasterVolumeLevelScalar(&volume);
#elif defined(__APPLE__)
  AudioDeviceID device =
      argv[0] == 0 ? GetDefaultOutputDevice() : GetDefaultInputDevice();
  UInt32 theSize = sizeof(volume);
  AudioObjectPropertyScope theScope = argv[0] == 1
                                          ? kAudioDevicePropertyScopeInput
                                          : kAudioDevicePropertyScopeOutput;
  AudioObjectPropertyAddress theAddress = {kAudioDevicePropertyVolumeScalar,
                                           theScope, 0};

  OSStatus theError = AudioObjectGetPropertyData(device, &theAddress, 0, NULL,
                                                 &theSize, &volume);
#endif

  return toValue(env, (int)round(volume * 100));
}

napi_value isMuted(napi_env env, napi_callback_info info) {
  int *argv = getArgs(env, info);
  int mute = 0;

#ifdef _WIN32
  ComInitializer ole;
  CComPtr<IAudioEndpointVolume> tmp_volume = getVolume(argv[0]);

  if (tmp_volume == NULL) {
    return toValue(env, -999);
  }

  tmp_volume->GetMute(&mute);
#elif defined(__APPLE__)
  AudioDeviceID device =
      argv[0] == 0 ? GetDefaultOutputDevice() : GetDefaultInputDevice();
  UInt32 theSize = sizeof(mute);
  AudioObjectPropertyScope theScope = argv[0] == 1
                                          ? kAudioDevicePropertyScopeInput
                                          : kAudioDevicePropertyScopeOutput;
  AudioObjectPropertyAddress theAddress = {kAudioDevicePropertyMute, theScope,
                                           0};

  OSStatus theError =
      AudioObjectGetPropertyData(device, &theAddress, 0, NULL, &theSize, &mute);
#endif
  return toValue(env, mute);
}

napi_value mute(napi_env env, napi_callback_info info) {
  int *argv = getArgs(env, info);

#ifdef _WIN32
  ComInitializer ole;
  CComPtr<IAudioEndpointVolume> tmp_volume = getVolume(argv[0]);
  if (tmp_volume == NULL) {
    return toValue(env, -1);
  }

  tmp_volume->SetMute(argv[1], NULL);
#elif defined(__APPLE__)
  AudioDeviceID device =
      argv[0] == 0 ? GetDefaultOutputDevice() : GetDefaultInputDevice();
  UInt32 muted = argv[1];
  UInt32 theSize = sizeof(muted);
  AudioObjectPropertyScope theScope = argv[0] == 1
                                          ? kAudioDevicePropertyScopeInput
                                          : kAudioDevicePropertyScopeOutput;
  AudioObjectPropertyAddress theAddress = {kAudioDevicePropertyMute, theScope,
                                           0};

  OSStatus theError =
      AudioObjectSetPropertyData(device, &theAddress, 0, NULL, theSize, &muted);
#endif
  return toValue(env, 1);
}

napi_value set(napi_env env, napi_callback_info info) {
  int *argv = getArgs(env, info);
  float newVolume = ((float)argv[0]) / 100.0f;
  int mic = argv[1];
#ifdef _WIN32
  ComInitializer ole;
  CComPtr<IAudioEndpointVolume> tmp_volume = getVolume(mic);
  if (tmp_volume == NULL) {
    return toValue(env, -1);
  }

  tmp_volume->SetMasterVolumeLevelScalar(newVolume, NULL);
#elif defined(__APPLE__)
  AudioDeviceID device =
      mic == 0 ? GetDefaultOutputDevice() : GetDefaultInputDevice();
  Float32 volume = newVolume;
  UInt32 theSize = sizeof(volume);
  AudioObjectPropertyScope theScope = argv[1] == 1
                                          ? kAudioDevicePropertyScopeInput
                                          : kAudioDevicePropertyScopeOutput;
  AudioObjectPropertyAddress theAddress = {kAudioDevicePropertyVolumeScalar,
                                           theScope, 0};

  OSStatus theError = AudioObjectSetPropertyData(device, &theAddress, 0, NULL,
                                                 theSize, &volume);
#endif
  return toValue(env, 1);
}

static napi_status get_utf8_string(napi_env env, napi_value str,
                                   std::string *s) {
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

napi_value fixup_webm(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_status res;
  int r;
  size_t len;
  std::string in, out;
  napi_value result = nullptr;

  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 2) {
    napi_throw_error(env, "EINVAL", "Expected string: input file path");
    return nullptr;
  }

  res = get_utf8_string(env, argv[0], &in);
  if (res != napi_ok) {
    napi_throw_error(env, "EINVAL", "Something went wrong.");
    return nullptr;
  }
  res = get_utf8_string(env, argv[1], &out);
  if (res != napi_ok) {
    napi_throw_error(env, "EINVAL", "Something went wrong.");
    return nullptr;
  }
  r = FixWebmFile(in.c_str(), out.c_str());
  res = napi_create_int32(env, r, &result);
  if (res != napi_ok) {
    napi_throw_error(env, "EINVAL", "Something went wrong.");
    return nullptr;
  }

  return result;
}

typedef struct {
  std::string in;
  std::string out;
  int errcode;
  napi_async_work work;
  napi_deferred deferred;
} fixup_webm_work_t;

// This function runs on a worker thread. It has no access to the JavaScript.
static void ExecuteWork(napi_env env, void *data) {
  fixup_webm_work_t *work = (fixup_webm_work_t *)data;
  work->errcode = FixWebmFile(work->in.c_str(), work->out.c_str());
}

// This function runs on the main thread after `ExecuteWork` exits.
static void WorkComplete(napi_env env, napi_status status, void *data) {
  if (status != napi_ok) {
    return;
  }

  fixup_webm_work_t *work = (fixup_webm_work_t *)data;

  napi_value res = 0;
  napi_create_int32(env, work->errcode, &res);

  CHECK(napi_resolve_deferred(env, work->deferred, res) == napi_ok);

  // Clean up the work item associated with this run.
  CHECK(napi_delete_async_work(env, work->work) == napi_ok);

  delete work;
}

static napi_value fixup_webm_async(napi_env env, napi_callback_info info) {
  napi_value work_name, promise;
  fixup_webm_work_t *work = new fixup_webm_work_t();
  size_t argc = 2;
  napi_value argv[2];
  napi_status res;

  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 2) {
    napi_throw_error(env, "EINVAL", "Expect 2 arguments");
    return NULL;
  }

  res = get_utf8_string(env, argv[0], &work->in);
  if (res != napi_ok) {
    napi_throw_error(env, "EINVAL", "Expect a input file path");
    goto err;
  }

  res = get_utf8_string(env, argv[1], &work->out);
  if (res != napi_ok) {
    napi_throw_error(env, "EINVAL", "Expect a output file path");
    goto err;
  }

  CHECK(napi_create_promise(env, &work->deferred, &promise) == napi_ok);

  CHECK(napi_create_string_utf8(env, "Promise for fixup_webm_async",
                                NAPI_AUTO_LENGTH, &work_name) == napi_ok);

  CHECK(napi_create_async_work(env, nullptr, work_name, ExecuteWork,
                               WorkComplete, work, &work->work) == napi_ok);

  // Queue the work item for execution.
  CHECK(napi_queue_async_work(env, work->work) == napi_ok);

  return promise;

err:
  return NULL;
}

napi_value Init(napi_env env, napi_value exports) {
  napi_status status;
  napi_value get_fn, set_fn, mute_fn, is_mute_fn, fixup_webm_fn,
      fixup_webm_async_fn;

  status = napi_create_function(env, NULL, 0, get, NULL, &get_fn);
  status = napi_create_function(env, NULL, 0, set, NULL, &set_fn);
  status = napi_create_function(env, NULL, 0, mute, NULL, &mute_fn);
  status = napi_create_function(env, NULL, 0, isMuted, NULL, &is_mute_fn);
  status = napi_create_function(env, NULL, 0, fixup_webm, NULL, &fixup_webm_fn);
  status = napi_create_function(env, NULL, 0, fixup_webm_async, NULL,
                                &fixup_webm_async_fn);

  status = napi_set_named_property(env, exports, "get", get_fn);
  status = napi_set_named_property(env, exports, "set", set_fn);
  status = napi_set_named_property(env, exports, "mute", mute_fn);
  status = napi_set_named_property(env, exports, "isMuted", is_mute_fn);
  status = napi_set_named_property(env, exports, "fixup_webm", fixup_webm_fn);
  status = napi_set_named_property(env, exports, "fixup_webm_async",
                                   fixup_webm_async_fn);
  return exports;
}

NAPI_MODULE(addon, Init)
