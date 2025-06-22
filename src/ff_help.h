#pragma once

#include <stdarg.h>
#include <stdint.h>

#include "export.h"
#include <node_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <libavutil/avutil.h>

#ifndef EXPORT_AVHELP_API
// https://ffmpeg.org/doxygen/trunk/log_8h_source.html
#define AV_LOG_QUIET -8
#define AV_LOG_PANIC 0
#define AV_LOG_FATAL 8
#define AV_LOG_ERROR 16
#define AV_LOG_WARNING 24
#define AV_LOG_INFO 32
#define AV_LOG_VERBOSE 40
#define AV_LOG_DEBUG 48
#define AV_LOG_TRACE 56
#endif

typedef int (*read_packet_t)(void *ctx, uint8_t *buf, int buf_size);
typedef void (*log_callback_t)(void *ptr, int level, const char *fmt,
                               va_list vl);
EXPORTED int ff_run(int argc, const char **argv);
EXPORTED int ff_probe(int argc, const char **argv, char **out, int *out_size);
EXPORTED int ff_log_set(int level, log_callback_t callback);
EXPORTED void ff_log_reset(int level);
EXPORTED void ff_free(void *p);
EXPORTED int ff_get_av_duration(const char *inputFilePath, const char* password, enum AVMediaType mediaType,
                       int *duration);
EXPORTED int ff_get_av_duration_buffer(const uint8_t *buf, int buf_size,
                              enum AVMediaType mediaType, int *duration);
EXPORTED int ff_get_av_duration_callback(read_packet_t read_file, void *ctx,
                                enum AVMediaType mediaType, int *duration);

EXPORTED int ff_get_audio_volume(const char *filename, const char* password, int64_t start, int64_t duration,
                        float *max_volume, float *mean_volume);

EXPORTED int ff_get_audio_volume_buffer(const uint8_t *buf, int buf_size, int64_t start,
                               int64_t duration, float *max_volume,
                               float *mean_volume);

EXPORTED int ff_get_audio_volume_callback(read_packet_t read_packet, int64_t start,
                                 int64_t duration, float *max_volume,
                                 float *mean_volume, void *ctx);
#ifndef EXPORT_AVHELP_API
static const char *get_level_str(int level) {
  switch (level) {
  case AV_LOG_QUIET:
    return "quiet";
  case AV_LOG_DEBUG:
    return "debug";
  case AV_LOG_TRACE:
    return "trace";
  case AV_LOG_VERBOSE:
    return "verbose";
  case AV_LOG_INFO:
    return "info";
  case AV_LOG_WARNING:
    return "warning";
  case AV_LOG_ERROR:
    return "error";
  case AV_LOG_FATAL:
    return "fatal";
  case AV_LOG_PANIC:
    return "panic";
  default:
    return "";
  }
}
#endif

#ifdef __cplusplus
}
#endif
