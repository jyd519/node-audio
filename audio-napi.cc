#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#endif

#include <math.h>
#include <stdlib.h>
#include <node_api.h>

#ifdef __APPLE__
#include <unistd.h>
#include <CoreAudio/CoreAudio.h>

typedef enum {
  	kAudioTypeUnknown = 0,
    kAudioTypeInput   = 1,
    kAudioTypeOutput  = 2,
    kAudioTypeSystemOutput = 3
} ASDeviceType;

#endif

#ifdef __APPLE__
AudioDeviceID GetDefaultInputDevice()
{
    AudioDeviceID theAnswer = 0;
    UInt32 theSize = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress theAddress = { kAudioHardwarePropertyDefaultInputDevice,
                                              kAudioObjectPropertyScopeGlobal,
                                              kAudioObjectPropertyElementMaster };

    OSStatus theError = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                                   &theAddress,
                                                   0,
                                                   NULL,
                                                   &theSize,
                                                   &theAnswer);
    // handle errors

    return theAnswer;
}

AudioDeviceID GetDefaultOutputDevice()
{
    AudioDeviceID theAnswer = 0;
    UInt32 theSize = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress theAddress = { kAudioHardwarePropertyDefaultOutputDevice,
                                              kAudioObjectPropertyScopeGlobal,
                                              kAudioObjectPropertyElementMaster };

    OSStatus theError = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                                   &theAddress,
                                                   0,
                                                   NULL,
                                                   &theSize,
                                                   &theAnswer);
    // handle errors
    return theAnswer;
}
#endif

#ifdef _WIN32
IAudioEndpointVolume* getVolume(int mic){
  HRESULT hr;
  IMMDeviceEnumerator *enumerator = NULL;
  IAudioEndpointVolume *volume = NULL;
  IMMDevice *defaultDevice = NULL;
  CoInitialize(NULL);
  hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID *) &enumerator);
  hr = enumerator->GetDefaultAudioEndpoint(mic ? eCapture : eRender, eConsole, &defaultDevice);
  if (hr != 0) {
    return volume;
  }
  hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID *) &volume);
  enumerator->Release();
  defaultDevice->Release();
  CoUninitialize();
  return volume;
}
#endif

int *getArgs(napi_env env, napi_callback_info info){
  napi_value argv[2];
  size_t argc = 2;
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  int *out = (int*) malloc(sizeof(int) * argc);
  for(int i = 0; i < (int)argc; i++){
     napi_get_value_int32(env, argv[i], &out[i]);
  }
  return out;
}

napi_value toValue(napi_env env, int value){
  napi_value nvalue = 0;
  napi_create_int32(env, value, &nvalue);
  return nvalue;
}

napi_value get(napi_env env, napi_callback_info info) {

  int *argv = getArgs(env, info);
  float volume = 0;

#ifdef _WIN32
  IAudioEndpointVolume *tmp_volume = getVolume(argv[0]);

  if (tmp_volume == NULL) {
    return toValue(env, -1);
  }

  tmp_volume->GetMasterVolumeLevelScalar(&volume);
#elif defined(__APPLE__)
    AudioDeviceID device = argv[0] == 0? GetDefaultOutputDevice() : GetDefaultInputDevice();
    UInt32 theSize = sizeof(volume);
    AudioObjectPropertyScope theScope = argv[0] == 1 ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput;
    AudioObjectPropertyAddress theAddress = { kAudioDevicePropertyVolumeScalar,
        theScope,
        0 };
    
    OSStatus theError = AudioObjectGetPropertyData(device,
                                                   &theAddress,
                                                   0,
                                                   NULL,
                                                   &theSize,
                                                   &volume);
#endif

  return toValue(env, (int) round(volume*100));
}

napi_value isMuted(napi_env env, napi_callback_info info) {

  int *argv = getArgs(env, info);
  int mute = 0;

#ifdef _WIN32
  IAudioEndpointVolume *tmp_volume = getVolume(argv[0]);

  if (tmp_volume == NULL) {
    return toValue(env, -999);
  }

  tmp_volume->GetMute(&mute);
#elif defined(__APPLE__)
    AudioDeviceID device = argv[0] == 0? GetDefaultOutputDevice() : GetDefaultInputDevice();
    UInt32 theSize = sizeof(mute);
    AudioObjectPropertyScope theScope = argv[0] == 1 ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput;
    AudioObjectPropertyAddress theAddress = { kAudioDevicePropertyMute,
        theScope,
        0 };
    
    OSStatus theError = AudioObjectGetPropertyData(device,
                                                   &theAddress,
                                                   0,
                                                   NULL,
                                                   &theSize,
                                                   &mute);
#endif
  return toValue(env, mute);

}

napi_value mute(napi_env env, napi_callback_info info) {

  int *argv = getArgs(env, info);

#ifdef _WIN32
  IAudioEndpointVolume *tmp_volume = getVolume(argv[0]);

  if (tmp_volume == NULL) {
    return toValue(env, -1);
  }

  tmp_volume->SetMute(argv[1], NULL);
#elif defined(__APPLE__)
    AudioDeviceID device = argv[0] == 0? GetDefaultOutputDevice() : GetDefaultInputDevice();
    UInt32 muted = argv[1];
    UInt32 theSize = sizeof(muted);
    AudioObjectPropertyScope theScope = argv[0] == 1 ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput;
    AudioObjectPropertyAddress theAddress = { kAudioDevicePropertyMute,
        theScope,
        0 };
    
    OSStatus theError = AudioObjectSetPropertyData(device,
                                                   &theAddress,
                                                   0,
                                                   NULL,
                                                   theSize,
                                                   &muted);
#endif
  return toValue(env, 1);

}

napi_value set(napi_env env, napi_callback_info info) {

  int *argv = getArgs(env, info);
  float newVolume = ((float)argv[0])/100.0f;

#ifdef _WIN32
  IAudioEndpointVolume *tmp_volume = getVolume(mic);

  if (tmp_volume == NULL) {
    return toValue(env, -1);
  }

  tmp_volume->SetMasterVolumeLevelScalar(newVolume, NULL);
#elif defined(__APPLE__)
    AudioDeviceID device = argv[1] == 0? GetDefaultOutputDevice() : GetDefaultInputDevice();
    Float32 volume = newVolume;
    UInt32 theSize = sizeof(volume);
    AudioObjectPropertyScope theScope = argv[1] == 1 ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput;
    AudioObjectPropertyAddress theAddress = { kAudioDevicePropertyVolumeScalar,
        theScope,
        0 };
    
    OSStatus theError = AudioObjectSetPropertyData(device,
                                                   &theAddress,
                                                   0,
                                                   NULL,
                                                   theSize,
                                                   &volume);
#endif
  return toValue(env, 1);

}

napi_value Init(napi_env env, napi_value exports) {

  napi_status status;
  napi_value get_fn, set_fn, mute_fn, is_mute_fn;

  status = napi_create_function(env, NULL, 0, get, NULL, &get_fn);
  status = napi_create_function(env, NULL, 0, set, NULL, &set_fn);
  status = napi_create_function(env, NULL, 0, mute, NULL, &mute_fn);
  status = napi_create_function(env, NULL, 0, isMuted, NULL, &is_mute_fn);

  status = napi_set_named_property(env, exports, "get", get_fn);
  status = napi_set_named_property(env, exports, "set", set_fn);
  status = napi_set_named_property(env, exports, "mute", mute_fn);
  status = napi_set_named_property(env, exports, "isMuted", is_mute_fn);

  return exports;
}

NAPI_MODULE(addon, Init)
