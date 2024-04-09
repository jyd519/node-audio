#include "ff_help.h"

#include <libavformat/avformat.h>

#include "buffer_io.h"

int ff_get_av_duration_buffer(const uint8_t *buf, int buf_size,
                              enum AVMediaType mediaType, int *duration) {
  AVFormatContext *fmt_ctx = NULL;
  AVIOContext *avio_ctx = NULL;
  uint8_t *avio_ctx_buffer = NULL;
  size_t avio_ctx_buffer_size = 4096;
  int ret = 0;
  struct buffer_data bd = {0};
  double sec_duration = 0;
  int stream_index = -1;

  bd.ptr = (uint8_t *)buf;
  bd.size = buf_size;
  *duration = 0;

  if (!(fmt_ctx = avformat_alloc_context())) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  avio_ctx_buffer = (uint8_t*)av_malloc(avio_ctx_buffer_size);
  if (!avio_ctx_buffer) {
    ret = AVERROR(ENOMEM);
    goto end;
  }
  avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 0, &bd,
                                &read_packet, NULL, NULL);
  if (!avio_ctx) {
    ret = AVERROR(ENOMEM);
    goto end;
  }
  fmt_ctx->pb = avio_ctx;

  if ((ret = avformat_open_input(&fmt_ctx, NULL, NULL, NULL)) < 0) {
    goto end;
  }

  // Retrieve stream information
  if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
    goto end;
  }

  // Find the first matched stream
  for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == mediaType) {
      stream_index = i;
      break;
    }
  }
  if (stream_index == -1) {
    ret = AVERROR(ENOENT);
    goto end;
  }

  // Get the duration of the specified stream in seconds
  if (fmt_ctx->streams[stream_index]->duration != AV_NOPTS_VALUE) {
    sec_duration = (double)(fmt_ctx->duration) / AV_TIME_BASE;
  } else {
    sec_duration = (double)(fmt_ctx->duration) / AV_TIME_BASE;
  }

  *duration = (int)(ceil(sec_duration));

end:
  // Close the file
  avformat_close_input(&fmt_ctx);
  return ret;
}

int ff_get_av_duration_callback(read_packet_t read_file, void* ctx,
                                enum AVMediaType mediaType, int *duration) {

  AVFormatContext *fmt_ctx = NULL;
  AVIOContext *avio_ctx = NULL;
  uint8_t *avio_ctx_buffer = NULL;
  size_t avio_ctx_buffer_size = 4096;
  int ret = 0;
  double sec_duration = 0;
  int stream_index = -1;

  *duration = 0;

  if (!(fmt_ctx = avformat_alloc_context())) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  avio_ctx_buffer = (uint8_t*)av_malloc(avio_ctx_buffer_size);
  if (!avio_ctx_buffer) {
    ret = AVERROR(ENOMEM);
    goto end;
  }
  avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 0, ctx,
                                read_file, NULL, NULL);
  if (!avio_ctx) {
    ret = AVERROR(ENOMEM);
    goto end;
  }
  fmt_ctx->pb = avio_ctx;

  if ((ret = avformat_open_input(&fmt_ctx, NULL, NULL, NULL)) < 0) {
    goto end;
  }

  // Retrieve stream information
  if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
    goto end;
  }

  // Find the first matched stream
  for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == mediaType) {
      stream_index = i;
      break;
    }
  }
  if (stream_index == -1) {
    ret = AVERROR(ENOENT);
    goto end;
  }

  // Get the duration of the specified stream in seconds
  if (fmt_ctx->streams[stream_index]->duration != AV_NOPTS_VALUE) {
    sec_duration = (double)(fmt_ctx->duration) / AV_TIME_BASE;
  } else {
    sec_duration = (double)(fmt_ctx->duration) / AV_TIME_BASE;
  }

  *duration = (int)(ceil(sec_duration));

end:
  // Close the file
  avformat_close_input(&fmt_ctx);
  return ret;
}

int ff_get_av_duration(const char *inputFilePath, enum AVMediaType mediaType,
                       int *duration) {
  AVFormatContext *fmt_ctx = NULL;
  int ret = 0;
  int stream_index = -1;
  double sec_duration = 0;

  *duration = 0;

  // Open the media file
  if ((ret = avformat_open_input(&fmt_ctx, inputFilePath, NULL, NULL)) != 0) {
    goto end;
  }

  // Retrieve stream information
  if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
    goto end;
  }

  // Find the first matched stream
  for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == mediaType) {
      stream_index = i;
      break;
    }
  }

  if (stream_index == -1) {
    ret = AVERROR(ENOENT);
    goto end;
  }

  // Get the duration of the specified stream in seconds
  if (fmt_ctx->streams[stream_index]->duration != AV_NOPTS_VALUE) {
    sec_duration = (double)(fmt_ctx->duration) / AV_TIME_BASE;
  } else {
    sec_duration = (double)(fmt_ctx->duration) / AV_TIME_BASE;
  }

  *duration = (int)(ceil(sec_duration));

end:
  // Close the file
  avformat_close_input(&fmt_ctx);
  return ret;
}
