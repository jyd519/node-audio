#ifndef WEBM_MUXER_H
#define WEBM_MUXER_H

#if defined(_MSC_VER)
#define DLL_EXPORTED __declspec(dllexport)
#else
#define DLL_EXPORTED __attribute__((__visibility__("default")))
#endif 

extern "C" int DLL_EXPORTED FixWebmFile(const char* input, const char* output);
extern "C" int DLL_EXPORTED FixWebmFile2(const char *input, const char *output, const char* metadata);
#endif //WEBM_MUXER_H
