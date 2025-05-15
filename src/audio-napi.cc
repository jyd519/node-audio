#ifdef _WIN32
#include <atlbase.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <windows.h>
#endif

#include <math.h>
#include <node_api.h>
#include <stdlib.h>

#include <napi.h>

#ifdef ENABLE_FFMPEG
extern "C" {
#include <libavutil/avutil.h>
}
#endif

#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <unistd.h>
#endif

#ifdef __linux__
#include <alsa/asoundlib.h>
#include <alsa/version.h>

#define LOGMODE 0

#if defined(LOGMODE)
#define LOG(...) printf(__VA_ARGS__)
#else
#define LOG(...) void
#endif

#endif

#include "napi_help.h"
#include "addon_api.h"

#ifdef ENABLE_FFMPEG
#include "recorder_api.h"
#else
#include "webm_muxer.h"
#endif

#define CHECK(expr)                                                                                \
  {                                                                                                \
    if ((expr) == 0) {                                                                             \
      fprintf(stderr, "%s:%d: failed assertion `%s'\n", __FILE__, __LINE__, #expr);                \
      fflush(stderr);                                                                              \
      abort();                                                                                     \
    }                                                                                              \
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
  AudioObjectPropertyAddress theAddress = {kAudioHardwarePropertyDefaultInputDevice,
                                           kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMaster};

  OSStatus theError = AudioObjectGetPropertyData(kAudioObjectSystemObject, &theAddress, 0, NULL,
                                                 &theSize, &theAnswer);
  // handle errors

  return theAnswer;
}

AudioDeviceID GetDefaultOutputDevice() {
  AudioDeviceID theAnswer = 0;
  UInt32 theSize = sizeof(AudioDeviceID);
  AudioObjectPropertyAddress theAddress = {kAudioHardwarePropertyDefaultOutputDevice,
                                           kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMaster};

  OSStatus theError = AudioObjectGetPropertyData(kAudioObjectSystemObject, &theAddress, 0, NULL,
                                                 &theSize, &theAnswer);
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

  hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
                        __uuidof(IMMDeviceEnumerator), (LPVOID *)&enumerator);
  if (FAILED(hr)) {
    goto clean;
  }
  hr = enumerator->GetDefaultAudioEndpoint(mic ? eCapture : eRender, eConsole, &defaultDevice);
  if (FAILED(hr)) {
    goto clean;
  }
  hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL,
                               (LPVOID *)&volume);
  if (FAILED(hr)) {
    goto clean;
  }

clean:
  if (enumerator)
    enumerator->Release();
  if (defaultDevice)
    defaultDevice->Release();
  return volume;
}
#endif

#if defined(__linux__)

const int DEV_PLAYBACK = 0;
const int DEV_CAPTURE = 1;

static int alsamixer_gethandle(const char *cardname, snd_mixer_t **handle) {
  int err;
  if ((err = snd_mixer_open(handle, 0)) < 0) {
    LOG("snd_mixer_open failed: %d\n", err);
    return err;
  }

  if ((err = snd_mixer_attach(*handle, cardname)) >= 0 &&
      (err = snd_mixer_selem_register(*handle, NULL, NULL)) >= 0 &&
      (err = snd_mixer_load(*handle)) >= 0)
    return 0;
  snd_mixer_close(*handle);
  return err;
}

bool getAlsaMasterVolume(int dev, int *volume) {
  long min, max;
  long value = 0;
  snd_mixer_t *handle = nullptr;
  snd_mixer_selem_id_t *sid;
  snd_mixer_elem_t *elem = nullptr;
  const char *card = "default";
  const char *selem_name = dev == DEV_PLAYBACK ? "Master" : "Capture";
  bool success = false;

  if (alsamixer_gethandle(card, &handle) < 0) {
    return false;
  }

  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_id_set_index(sid, 0);
  snd_mixer_selem_id_set_name(sid, selem_name);
  elem = snd_mixer_find_selem(handle, sid);
  if (!elem) {
    goto exit;
  }

  if (dev == DEV_PLAYBACK) {
    if (snd_mixer_selem_get_playback_volume_range(elem, &min, &max) < 0) {
      goto exit;
    }
    if (snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &value)) {
      goto exit;
    }
    LOG("getAlsaMasterVolume : max: %ld, min=%ld, val = %ld, %f\n", max, min, value,
        (value - min) * 100.0 / (max - min));
    *volume = round((value - min) * 100.0 / (max - min));
  } else {
    if (snd_mixer_selem_get_capture_volume_range(elem, &min, &max) < 0) {
      LOG("snd_mixer_selem_get_capture_volume_range failed\n");
      goto exit;
    }
    if (snd_mixer_selem_get_capture_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &value) < 0) {
      goto exit;
    }
    LOG("getAlsaMasterVolume : max: %ld, min=%ld, val = %ld, %f\n", max, min, value,
        (value - min) * 100.0 / (max - min));
    *volume = round((value - min) * 100.0 / (max - min));
  }

  success = true;
exit:
  snd_mixer_close(handle);
  return success;
}

// volume: 0~1.0
bool setAlsaMasterVolume(int dev, float volume) {
  long min, max;
  long value = 0;
  snd_mixer_t *handle = nullptr;
  snd_mixer_selem_id_t *sid;
  const char *card = "default";
  const char *selem_name = dev == DEV_PLAYBACK ? "Master" : "Capture";
  snd_mixer_elem_t *elem = nullptr;
  bool success = false;

  if (alsamixer_gethandle(card, &handle) < 0) {
    return false;
  }

  LOG("setAlsaMasterVolume : %d, %f\n", dev, volume);
  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_id_set_index(sid, 0);
  snd_mixer_selem_id_set_name(sid, selem_name);
  elem = snd_mixer_find_selem(handle, sid);
  if (!elem) {
    goto exit;
  }

  if (dev == DEV_PLAYBACK) {
    if (snd_mixer_selem_get_playback_volume_range(elem, &min, &max) < 0) {
      goto exit;
    }
    LOG("setAlsaMasterVolume : max: %ld, min=%ld, val = %d\n", max, min,
        int(volume * (max - min) + min));
    if (snd_mixer_selem_set_playback_volume_all(elem, int(volume * (max - min) + min)) < 0) {
      goto exit;
    }
  } else {
    if (snd_mixer_selem_get_capture_volume_range(elem, &min, &max) < 0) {
      goto exit;
    }
    LOG("setAlsaMasterVolume : max: %ld, min=%ld, val = %d\n", max, min,
        int(volume * (max - min) + min));
    if (snd_mixer_selem_set_capture_volume_all(elem, int(volume * (max - min) + min))) {
      goto exit;
    }
  }
  success = true;
exit:
  snd_mixer_close(handle);
  return success;
}

bool getMuteState(int dev, bool *muted) {
  snd_mixer_t *handle = nullptr;
  snd_mixer_selem_id_t *sid;
  const char *card = "default";
  const char *selem_name = dev == DEV_PLAYBACK ? "Master" : "Capture";
  snd_mixer_elem_t *elem = nullptr;
  snd_mixer_selem_channel_id_t channel = SND_MIXER_SCHN_FRONT_RIGHT;
  bool success = false;

  *muted = false;

  if (alsamixer_gethandle(card, &handle) < 0) {
    return false;
  }

  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_id_set_index(sid, 0);
  snd_mixer_selem_id_set_name(sid, selem_name);
  elem = snd_mixer_find_selem(handle, sid);
  if (!elem) {
    goto exit;
  }

  if (dev == DEV_PLAYBACK) {
    if (!snd_mixer_selem_has_playback_switch(elem)) {
      goto exit;
    }

    if (snd_mixer_selem_has_playback_channel(elem, channel)) {
      int value = 0;
      if (snd_mixer_selem_get_playback_switch(elem, channel, &value) < 0) {
        LOG("%s", "Can't get playback switch");
        goto exit;
      }
      /* Value returned: 0 = muted, 1 = not muted */
      *muted = value == 0 ? true : false;
    }
  } else {
    if (!snd_mixer_selem_has_capture_switch(elem)) {
      goto exit;
    }

    if (snd_mixer_selem_has_capture_channel(elem, channel)) {
      int value = 0;
      if (snd_mixer_selem_get_capture_switch(elem, channel, &value) < 0) {
        LOG("%s\n", "Can't get capture switch");
        goto exit;
      }
      /* Value returned: 0 = muted, 1 = not muted */
      *muted = value == 0 ? true : false;
    }
  }
  success = true;
exit:
  snd_mixer_close(handle);
  return success;
}

bool setMuteState(int dev, bool muted) {
  snd_mixer_t *handle = nullptr;
  snd_mixer_selem_id_t *sid;
  const char *card = "default";
  const char *selem_name = dev == DEV_PLAYBACK ? "Master" : "Capture";
  snd_mixer_elem_t *elem = nullptr;
  bool success = false;

  /* Value to set: 0 = muted, 1 = not muted */
  int value = muted ? 0 : 1;

  if (alsamixer_gethandle(card, &handle) < 0) {
    return false;
  }

  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_id_set_index(sid, 0);
  snd_mixer_selem_id_set_name(sid, selem_name);
  elem = snd_mixer_find_selem(handle, sid);
  if (!elem) {
    goto exit;
  }

  if (dev == DEV_PLAYBACK) {
    if (!snd_mixer_selem_has_playback_switch(elem)) {
      goto exit;
    }
    if (snd_mixer_selem_set_playback_switch_all(elem, value) < 0) {
      goto exit;
    }
  } else {
    if (!snd_mixer_selem_has_capture_switch(elem)) {
      goto exit;
    }
    if (snd_mixer_selem_set_capture_switch_all(elem, value) < 0) {
      goto exit;
    }
  }
  success = true;
exit:
  snd_mixer_close(handle);
  return success;
}
#endif

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
  AudioDeviceID device = argv[0] == 0 ? GetDefaultOutputDevice() : GetDefaultInputDevice();
  UInt32 theSize = sizeof(volume);
  AudioObjectPropertyScope theScope =
      argv[0] == 1 ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput;
  AudioObjectPropertyAddress theAddress = {kAudioDevicePropertyVolumeScalar, theScope, 0};

  OSStatus theError = AudioObjectGetPropertyData(device, &theAddress, 0, NULL, &theSize, &volume);
#elif defined(__linux__)
  int val = 0;
  getAlsaMasterVolume(argv[0], &val);
  return toValue(env, val);
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
  AudioDeviceID device = argv[0] == 0 ? GetDefaultOutputDevice() : GetDefaultInputDevice();
  UInt32 theSize = sizeof(mute);
  AudioObjectPropertyScope theScope =
      argv[0] == 1 ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput;
  AudioObjectPropertyAddress theAddress = {kAudioDevicePropertyMute, theScope, 0};

  OSStatus theError = AudioObjectGetPropertyData(device, &theAddress, 0, NULL, &theSize, &mute);
#elif defined(__linux__)
  bool is_muted = false;
  if (!getMuteState(argv[0], &is_muted)) {
    LOG("%s", "failed to get mute state\n");
  }
  mute = is_muted ? 1 : 0;
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
  AudioDeviceID device = argv[0] == 0 ? GetDefaultOutputDevice() : GetDefaultInputDevice();
  UInt32 muted = argv[1];
  UInt32 theSize = sizeof(muted);
  AudioObjectPropertyScope theScope =
      argv[0] == 1 ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput;
  AudioObjectPropertyAddress theAddress = {kAudioDevicePropertyMute, theScope, 0};

  OSStatus theError = AudioObjectSetPropertyData(device, &theAddress, 0, NULL, theSize, &muted);
#elif defined(__linux__)
  if (!setMuteState(argv[0], argv[1])) {
    LOG("failed to mute %d, %d\n", argv[0], argv[1]);
  }
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
  AudioDeviceID device = mic == 0 ? GetDefaultOutputDevice() : GetDefaultInputDevice();
  Float32 volume = newVolume;
  UInt32 theSize = sizeof(volume);
  AudioObjectPropertyScope theScope =
      argv[1] == 1 ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput;
  AudioObjectPropertyAddress theAddress = {kAudioDevicePropertyVolumeScalar, theScope, 0};

  OSStatus theError = AudioObjectSetPropertyData(device, &theAddress, 0, NULL, theSize, &volume);
#elif defined(__linux__)
  setAlsaMasterVolume(mic, newVolume);
#endif
  return toValue(env, 1);
}

#ifndef ENABLE_FFMPEG
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
  fixup_webm_work_t *work = (fixup_webm_work_t *)data;

  if (status != napi_cancelled) {
    napi_value res = 0;
    napi_create_int32(env, work->errcode, &res);

    CHECK(napi_resolve_deferred(env, work->deferred, res) == napi_ok);

    // Clean up the work item associated with this run.
    CHECK(napi_delete_async_work(env, work->work) == napi_ok);
  }

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

  CHECK(napi_create_string_utf8(env, "Promise for fixup_webm_async", NAPI_AUTO_LENGTH,
                                &work_name) == napi_ok);

  CHECK(napi_create_async_work(env, nullptr, work_name, ExecuteWork, WorkComplete, work,
                               &work->work) == napi_ok);

  // Queue the work item for execution.
  CHECK(napi_queue_async_work(env, work->work) == napi_ok);

  return promise;

err:
  return NULL;
}
#endif


static Napi::Object Init(Napi::Env env, Napi::Object exports) {
 InstanceData* instance = new InstanceData();
 env.SetInstanceData(instance);

 napi_status status = napi_ok;
#define ADD_FUNCTION(name)                                                                         \
  napi_value name##_fn;                                                                            \
  status = napi_create_function(env, NULL, 0, name, NULL, &name##_fn);                             \
  if (status == napi_ok) {                                                                         \
    napi_set_named_property(env, exports, #name, name##_fn);                              \
  }

  ADD_FUNCTION(get)
  ADD_FUNCTION(set)
  ADD_FUNCTION(mute)
  ADD_FUNCTION(isMuted)
#ifndef ENABLE_FFMPEG
  ADD_FUNCTION(fixup_webm)
  ADD_FUNCTION(fixup_webm_async)
#endif
#ifdef ENABLE_FFMPEG
  exports.Set("get_audio_duration", Napi::Function::New(env, get_audio_duration));
  exports.Set("get_audio_volume_info", Napi::Function::New(env, get_audio_volume_info));
  exports.Set("probe", Napi::Function::New(env, probe));
  exports.Set("record_screen", Napi::Function::New(env, record_screen));
  exports.Set("combine", Napi::Function::New(env, combine));
  exports.Set("fixup_webm", Napi::Function::New(env, fixwebmfile));
  exports.Set("fixup_webm_async", Napi::Function::New(env, fixwebmfileAsync));

  av_log_set_level(AV_LOG_ERROR);
#ifdef _WIN32
  // Use safer _dupenv_s on Windows platforms
  char* env_value = nullptr;
  size_t len = 0;
  errno_t err = _dupenv_s(&env_value, &len, "JOY_AVLOG");
  if (err == 0 && env_value != nullptr) {
    av_log_set_level(AV_LOG_DEBUG);
    free(env_value); // Free the allocated memory
  }
#else
  // Use standard getenv on non-Windows platforms
  if (getenv("JOY_AVLOG")) {
    av_log_set_level(AV_LOG_DEBUG);
  }
#endif

  Recorder::Init(env, exports);
#endif
  return exports;
}

NODE_API_MODULE(audio, Init)
