#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <stdio.h>
#endif

typedef int (*lpfnfixwebmfile)(const char* input, const char* output);
lpfnfixwebmfile pfnFixWebmFile = 0;

extern int FixWebmFile(char* in, char* out) { // wrapper function
  if (!pfnFixWebmFile) {
#ifdef _WIN32
    HMODULE h = LoadLibrary("webm.dll");
    if (!h) {
      return -1;
    }
    pfnFixWebmFile = (lpfnfixwebmfile)GetProcAddress(h, "FixWebmFile");
    if (!pfnFixWebmFile) {
      return -1;
    }
#else

  printf("loading libwebm.so ... \n");
	void* handle = dlopen("libwebm.so", RTLD_LAZY);
  if (!handle) {
    printf(" libwebm.so not found\n");
    return -1;
  }
	pfnFixWebmFile = (lpfnfixwebmfile)dlsym(handle, "FixWebmFile");
  if (!pfnFixWebmFile) {
    printf(" FixWebmFile not found\n");
    return -1;
  }
#endif
  }
  return pfnFixWebmFile(in, out);
}

