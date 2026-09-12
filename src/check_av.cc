#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/log.h>
#ifdef __cplusplus
}
#endif

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "ff_help.h"
#include "enc_reader.h"

#define CHECK_AV_MAX_DB 91

struct CheckAVResult {
  int has_video;
  int has_audio;
  double duration;
  double black_total;
  double freeze_total;
  double silence_total;
  double mean_volume;
};

typedef struct VolDetectCtx {
  uint64_t histogram[0x10001];
} VolDetectCtx;

static inline double vol_logdb(uint64_t v) {
  double d = v / (double)(0x8000 * 0x8000);
  if (!v)
    return CHECK_AV_MAX_DB;
  return -log10(d) * 10;
}

static void vol_process_frame(VolDetectCtx *vd, AVFrame *samples) {
  int nb_samples = samples->nb_samples;
#if LIBAVCODEC_VERSION_MAJOR > 58
  int nb_channels = samples->ch_layout.nb_channels;
#else
  int nb_channels = av_get_channel_layout_nb_channels(samples->channel_layout);
#endif
  int nb_planes = nb_channels;
  int plane, i;
  int16_t *pcm;

  if (!av_sample_fmt_is_planar((enum AVSampleFormat)samples->format)) {
    nb_samples *= nb_channels;
    nb_planes = 1;
  }
  for (plane = 0; plane < nb_planes; plane++) {
    pcm = (int16_t *)samples->extended_data[plane];
    for (i = 0; i < nb_samples; i++)
      vd->histogram[pcm[i] + 0x8000]++;
  }
}

static double vol_get_mean(VolDetectCtx *vd) {
  int i, shift;
  uint64_t nb_samples = 0, power = 0, nb_samples_shift = 0;

  for (i = 0; i < 0x10000; i++)
    nb_samples += vd->histogram[i];
  if (!nb_samples)
    return -CHECK_AV_MAX_DB;

  shift = av_log2(nb_samples >> 33);
  for (i = 0; i < 0x10000; i++) {
    nb_samples_shift += vd->histogram[i] >> shift;
    power += (i - 0x8000) * (i - 0x8000) * (vd->histogram[i] >> shift);
  }
  if (!nb_samples_shift)
    return -CHECK_AV_MAX_DB;
  power = (power + nb_samples_shift / 2) / nb_samples_shift;
  assert(power <= 0x8000 * 0x8000);
  return -vol_logdb(power);
}

static double get_meta_double(AVFrame *frame, const char *key) {
  AVDictionaryEntry *e = av_dict_get(frame->metadata, key, NULL, 0);
  if (e)
    return atof(e->value);
  return 0.0;
}

static int build_video_filter_graph(AVFilterGraph *graph, AVStream *stream,
                                    AVCodecContext *codec_ctx,
                                    AVFilterContext **src_ctx,
                                    AVFilterContext **sink_ctx) {
  int ret;
  char args[512];
  AVFilterContext *buffersrc_ctx = NULL;
  AVFilterContext *buffersink_ctx = NULL;
  AVFilterContext *blackdetect_ctx = NULL;
  AVFilterContext *freezedetect_ctx = NULL;

  const AVFilter *buffersrc = avfilter_get_by_name("buffer");
  const AVFilter *buffersink = avfilter_get_by_name("buffersink");
  const AVFilter *blackdetect = avfilter_get_by_name("blackdetect");
  const AVFilter *freezedetect = avfilter_get_by_name("freezedetect");

  AVRational time_base = stream->time_base;
  AVRational sar = stream->codecpar->sample_aspect_ratio;
  if (sar.num == 0)
    sar =  av_make_q(1,1);

  snprintf(args, sizeof(args),
           "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
           codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
           time_base.num, time_base.den, sar.num, sar.den);

  ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                     args, NULL, graph);
  if (ret < 0) goto end;

  ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                     NULL, NULL, graph);
  if (ret < 0) goto end;

  ret = avfilter_graph_create_filter(&blackdetect_ctx, blackdetect,
                                     "blackdetect", "d=0.1:pix_th=0.10",
                                     NULL, graph);
  if (ret < 0) goto end;

  ret = avfilter_graph_create_filter(&freezedetect_ctx, freezedetect,
                                     "freezedetect", "n=0.003:d=0.5",
                                     NULL, graph);
  if (ret < 0) goto end;

  ret = avfilter_link(buffersrc_ctx, 0, blackdetect_ctx, 0);
  if (ret < 0) goto end;
  ret = avfilter_link(blackdetect_ctx, 0, freezedetect_ctx, 0);
  if (ret < 0) goto end;
  ret = avfilter_link(freezedetect_ctx, 0, buffersink_ctx, 0);
  if (ret < 0) goto end;

  ret = avfilter_graph_config(graph, NULL);
  if (ret < 0) goto end;

  *src_ctx = buffersrc_ctx;
  *sink_ctx = buffersink_ctx;

end:
  return ret;
}

static int build_audio_filter_graph(AVFilterGraph *graph, AVStream *stream,
                                    AVFilterContext **src_ctx,
                                    AVFilterContext **sink_ctx) {
  int ret;
  char ch_layout[64];
  AVRational time_base;
  AVFilterContext *abuffer_ctx = NULL;
  AVFilterContext *abuffersink_ctx = NULL;
  AVFilterContext *aformat_ctx = NULL;
  AVFilterContext *silencedetect_ctx = NULL;

  const AVFilter *abuffer = avfilter_get_by_name("abuffer");
  const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
  const AVFilter *aformat = avfilter_get_by_name("aformat");
  const AVFilter *silencedetect = avfilter_get_by_name("silencedetect");

  abuffer_ctx = avfilter_graph_alloc_filter(graph, abuffer, "in");
  abuffersink_ctx = avfilter_graph_alloc_filter(graph, abuffersink, "out");
  aformat_ctx = avfilter_graph_alloc_filter(graph, aformat, "aformat");
  if (!abuffer_ctx || !abuffersink_ctx || !aformat_ctx) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

#if LIBAVCODEC_VERSION_MAJOR > 58
  av_channel_layout_describe(&stream->codecpar->ch_layout, ch_layout, sizeof(ch_layout));
#else
  int nb_channels = av_get_channel_layout_nb_channels(stream->codecpar->channel_layout);
  uint64_t channel_layout_mask = stream->codecpar->channel_layout;
  av_get_channel_layout_string(ch_layout, sizeof(ch_layout), nb_channels, channel_layout_mask);
#endif
  av_opt_set(abuffer_ctx, "channel_layout", ch_layout, AV_OPT_SEARCH_CHILDREN);
  av_opt_set(abuffer_ctx, "sample_fmt",
             av_get_sample_fmt_name((enum AVSampleFormat)stream->codecpar->format),
             AV_OPT_SEARCH_CHILDREN);
  av_opt_set_int(abuffer_ctx, "sample_rate", stream->codecpar->sample_rate,
                 AV_OPT_SEARCH_CHILDREN);
  time_base = av_make_q(1, stream->codecpar->sample_rate);
  ret = av_opt_set_q(abuffer_ctx, "time_base", time_base,
                     AV_OPT_SEARCH_CHILDREN);
  if (ret < 0) goto end;

  ret = avfilter_init_str(abuffer_ctx, NULL);
  if (ret < 0) goto end;

  ret = avfilter_init_str(abuffersink_ctx, NULL);
  if (ret < 0) goto end;

  av_opt_set(aformat_ctx, "sample_fmts", "s16|s16p", AV_OPT_SEARCH_CHILDREN);
  ret = avfilter_init_str(aformat_ctx, NULL);
  if (ret < 0) goto end;

  ret = avfilter_graph_create_filter(&silencedetect_ctx, silencedetect,
                                     "silencedetect", "noise=-50dB:d=2",
                                     NULL, graph);
  if (ret < 0) goto end;

  // abuffer -> aformat(s16) -> silencedetect -> abuffersink
  ret = avfilter_link(abuffer_ctx, 0, aformat_ctx, 0);
  if (ret < 0) goto end;
  ret = avfilter_link(aformat_ctx, 0, silencedetect_ctx, 0);
  if (ret < 0) goto end;
  ret = avfilter_link(silencedetect_ctx, 0, abuffersink_ctx, 0);
  if (ret < 0) goto end;

  ret = avfilter_graph_config(graph, NULL);
  if (ret < 0) goto end;

  *src_ctx = abuffer_ctx;
  *sink_ctx = abuffersink_ctx;

end:
  return ret;
}

static void prepare_audio_frame(AVFrame *frame, AVStream *stream) {
  AVRational time_base = av_make_q(1, frame->sample_rate);

  if (frame->pts != AV_NOPTS_VALUE)
    frame->pts = av_rescale_q(frame->pts, stream->time_base, time_base);
  frame->duration = frame->nb_samples;
  frame->time_base = time_base;
}

static int _check_av(AVFormatContext *fmt_ctx, double max_duration,
                     CheckAVResult *result) {
  int ret;
  int video_idx = -1, audio_idx = -1;
  AVStream *video_stream = NULL, *audio_stream = NULL;
  AVCodecContext *video_codec_ctx = NULL, *audio_codec_ctx = NULL;
  const AVCodec *video_codec = NULL, *audio_codec = NULL;
  AVFilterGraph *video_graph = NULL, *audio_graph = NULL;
  AVFilterContext *video_src = NULL, *video_sink = NULL;
  AVFilterContext *audio_src = NULL, *audio_sink = NULL;
  AVFrame *frame = NULL;
  AVPacket *packet = NULL;
  int64_t video_end_pts = AV_NOPTS_VALUE;
  int64_t audio_end_pts = AV_NOPTS_VALUE;
  VolDetectCtx *vd = NULL;
  double last_silence_start = -1;

  memset(result, 0, sizeof(CheckAVResult));
  result->mean_volume = -CHECK_AV_MAX_DB;

  ret = avformat_find_stream_info(fmt_ctx, NULL);
  if (ret < 0) return ret;

  for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_idx < 0)
      video_idx = i;
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_idx < 0)
      audio_idx = i;
  }

  result->has_video = (video_idx >= 0) ? 1 : 0;
  result->has_audio = (audio_idx >= 0) ? 1 : 0;

  if (fmt_ctx->duration != AV_NOPTS_VALUE)
    result->duration = (double)fmt_ctx->duration / AV_TIME_BASE;

  if (max_duration > 0 && max_duration < result->duration)
    result->duration = max_duration;

  if (max_duration > 0) {
    if (video_idx >= 0)
      video_end_pts = av_rescale_q((int64_t)(max_duration * AV_TIME_BASE),
                                   AV_TIME_BASE_Q, fmt_ctx->streams[video_idx]->time_base);
    if (audio_idx >= 0)
      audio_end_pts = av_rescale_q((int64_t)(max_duration * AV_TIME_BASE),
                                   AV_TIME_BASE_Q, fmt_ctx->streams[audio_idx]->time_base);
  }

  if (!result->has_video && !result->has_audio)
    return 0;

  frame = av_frame_alloc();
  packet = av_packet_alloc();
  if (!frame || !packet) { ret = AVERROR(ENOMEM); goto end; }

  if (video_idx >= 0) {
    video_stream = fmt_ctx->streams[video_idx];
    video_codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!video_codec) { ret = AVERROR(ENOENT); goto end; }
    video_codec_ctx = avcodec_alloc_context3(video_codec);
    if (!video_codec_ctx) { ret = AVERROR(ENOMEM); goto end; }
    avcodec_parameters_to_context(video_codec_ctx, video_stream->codecpar);
    video_codec_ctx->pkt_timebase = video_stream->time_base;
    ret = avcodec_open2(video_codec_ctx, video_codec, NULL);
    if (ret < 0) goto end;

    video_graph = avfilter_graph_alloc();
    if (!video_graph) { ret = AVERROR(ENOMEM); goto end; }
    ret = build_video_filter_graph(video_graph, video_stream, video_codec_ctx,
                                   &video_src, &video_sink);
    if (ret < 0) {
      av_log(NULL, AV_LOG_WARNING, "check_av: video filter graph init failed, skip video check");
      avfilter_graph_free(&video_graph);
      video_graph = NULL;
    }
  }

  if (audio_idx >= 0) {
    audio_stream = fmt_ctx->streams[audio_idx];
    audio_codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (!audio_codec) { ret = AVERROR(ENOENT); goto end; }
    audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    if (!audio_codec_ctx) { ret = AVERROR(ENOMEM); goto end; }
    avcodec_parameters_to_context(audio_codec_ctx, audio_stream->codecpar);
    audio_codec_ctx->pkt_timebase = audio_stream->time_base;
    ret = avcodec_open2(audio_codec_ctx, audio_codec, NULL);
    if (ret < 0) goto end;

    audio_graph = avfilter_graph_alloc();
    if (!audio_graph) { ret = AVERROR(ENOMEM); goto end; }
    ret = build_audio_filter_graph(audio_graph, audio_stream,
                                   &audio_src, &audio_sink);
    if (ret < 0) {
      av_log(NULL, AV_LOG_WARNING, "check_av: audio filter graph init failed, skip audio check");
      avfilter_graph_free(&audio_graph);
      audio_graph = NULL;
    } else {
      vd = (VolDetectCtx *)av_mallocz(sizeof(VolDetectCtx));
    }
  }

  while ((ret = av_read_frame(fmt_ctx, packet)) >= 0) {
    if (packet->stream_index == video_idx && video_graph) {
      if (video_end_pts != AV_NOPTS_VALUE && packet->pts >= video_end_pts) {
        av_packet_unref(packet);
        continue;
      }
      ret = avcodec_send_packet(video_codec_ctx, packet);
      if (ret < 0) { av_packet_unref(packet); continue; }

      while (ret >= 0) {
        ret = avcodec_receive_frame(video_codec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) goto end;

        ret = av_buffersrc_add_frame(video_src, frame);
        if (ret < 0) { av_frame_unref(frame); break; }

        while (1) {
          ret = av_buffersink_get_frame(video_sink, frame);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
          if (ret < 0) goto end;

          double bd = get_meta_double(frame, "lavfi.black_duration");
          if (bd > 0) result->black_total += bd;

          double fd = get_meta_double(frame, "lavfi.freezedetect.freeze_duration");
          if (fd > 0) result->freeze_total += fd;

          av_frame_unref(frame);
        }
      }
    } else if (packet->stream_index == audio_idx && audio_graph) {
      if (audio_end_pts != AV_NOPTS_VALUE && packet->pts >= audio_end_pts) {
        av_packet_unref(packet);
        continue;
      }
      ret = avcodec_send_packet(audio_codec_ctx, packet);
      if (ret < 0) { av_packet_unref(packet); continue; }

      while (ret >= 0) {
        ret = avcodec_receive_frame(audio_codec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) goto end;

        prepare_audio_frame(frame, audio_stream);
        ret = av_buffersrc_add_frame(audio_src, frame);
        if (ret < 0) { av_frame_unref(frame); break; }

        while (1) {
          ret = av_buffersink_get_frame(audio_sink, frame);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
          if (ret < 0) goto end;

          double ss = get_meta_double(frame, "lavfi.silence_start");
          if (ss > 0 || av_dict_get(frame->metadata, "lavfi.silence_start", NULL, 0))
            last_silence_start = ss;
          double sd = get_meta_double(frame, "lavfi.silence_duration");
          if (sd > 0) {
            result->silence_total += sd;
            last_silence_start = -1;
          }
          if (vd) vol_process_frame(vd, frame);

          av_frame_unref(frame);
        }
      }
    }
    av_packet_unref(packet);
  }

  /* Flush decoders */
  if (video_codec_ctx && video_graph) {
    avcodec_send_packet(video_codec_ctx, NULL);
    while (avcodec_receive_frame(video_codec_ctx, frame) >= 0) {
      av_buffersrc_add_frame(video_src, frame);
      while (av_buffersink_get_frame(video_sink, frame) >= 0) {
        double bd = get_meta_double(frame, "lavfi.black_duration");
        if (bd > 0) result->black_total += bd;
        double fd = get_meta_double(frame, "lavfi.freezedetect.freeze_duration");
        if (fd > 0) result->freeze_total += fd;
        av_frame_unref(frame);
      }
    }
    av_buffersrc_add_frame(video_src, NULL);
    while (av_buffersink_get_frame(video_sink, frame) >= 0) {
      double bd = get_meta_double(frame, "lavfi.black_duration");
      if (bd > 0) result->black_total += bd;
      double fd = get_meta_double(frame, "lavfi.freezedetect.freeze_duration");
      if (fd > 0) result->freeze_total += fd;
      av_frame_unref(frame);
    }
  }

  if (audio_codec_ctx && audio_graph) {
    avcodec_send_packet(audio_codec_ctx, NULL);
    while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
      prepare_audio_frame(frame, audio_stream);
      av_buffersrc_add_frame(audio_src, frame);
      while (av_buffersink_get_frame(audio_sink, frame) >= 0) {
        double ss = get_meta_double(frame, "lavfi.silence_start");
        if (ss > 0 || av_dict_get(frame->metadata, "lavfi.silence_start", NULL, 0))
          last_silence_start = ss;
        double sd = get_meta_double(frame, "lavfi.silence_duration");
        if (sd > 0) {
          result->silence_total += sd;
          last_silence_start = -1;
        }
        if (vd) vol_process_frame(vd, frame);
        av_frame_unref(frame);
      }
    }
    av_buffersrc_add_frame(audio_src, NULL);
    while (av_buffersink_get_frame(audio_sink, frame) >= 0) {
      double ss = get_meta_double(frame, "lavfi.silence_start");
      if (ss > 0 || av_dict_get(frame->metadata, "lavfi.silence_start", NULL, 0))
        last_silence_start = ss;
      double sd = get_meta_double(frame, "lavfi.silence_duration");
      if (sd > 0) {
        result->silence_total += sd;
        last_silence_start = -1;
      }
      if (vd) vol_process_frame(vd, frame);
      av_frame_unref(frame);
    }
  }

  if (last_silence_start >= 0 && result->duration > last_silence_start)
    result->silence_total += result->duration - last_silence_start;

  if (result->silence_total < 0)
    result->silence_total = 0;
  if (result->silence_total > result->duration)
    result->silence_total = result->duration;

  if (vd) {
    result->mean_volume = vol_get_mean(vd);
    av_free(vd);
  }

  if (ret == AVERROR_EOF)
    ret = 0;

end:
  if (video_codec_ctx) avcodec_free_context(&video_codec_ctx);
  if (audio_codec_ctx) avcodec_free_context(&audio_codec_ctx);
  avfilter_graph_free(&video_graph);
  avfilter_graph_free(&audio_graph);
  av_packet_free(&packet);
  av_frame_free(&frame);
  return ret;
}

EXPORTED int ff_check_av(const char *filename, const char *password,
                         double max_duration,
                         int *has_video, int *has_audio,
                         double *duration,
                         double *black_total, double *freeze_total,
                         double *silence_total, double *mean_volume) {
  AVFormatContext *fmt_ctx = NULL;
  AVIOContext *avio_ctx = NULL;
  unsigned char *avio_ctx_buffer = NULL;
  av::CustomIO *io = NULL;
  int avio_ctx_buffer_size = 32 * 1024;
  int ret = -1;
  CheckAVResult result = {0};

  if (is_enc_file(filename)) {
    io = new EncryptReader(filename, password);
    if (!(fmt_ctx = avformat_alloc_context())) {
      ret = AVERROR(ENOMEM);
      goto end;
    }
    avio_ctx_buffer = (unsigned char *)av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
      ret = AVERROR(ENOMEM);
      goto end;
    }
    avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 0,
                                  (void *)io, &customio_read, NULL, &customio_seek);
    if (!avio_ctx) {
      ret = AVERROR(ENOMEM);
      goto end;
    }
    fmt_ctx->pb = avio_ctx;
    fmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
  }

  if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) != 0) {
    ret = AVERROR(EINVAL);
    goto end;
  }

  ret = _check_av(fmt_ctx, max_duration, &result);

  *has_video = result.has_video;
  *has_audio = result.has_audio;
  *duration = result.duration;
  *black_total = result.black_total;
  *freeze_total = result.freeze_total;
  *silence_total = result.silence_total;
  *mean_volume = result.mean_volume;

end:
  avformat_close_input(&fmt_ctx);
  if (avio_ctx) {
    av_freep(&avio_ctx->buffer);
    avio_context_free(&avio_ctx);
  }
  delete io;
  return ret;
}
