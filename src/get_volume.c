#include <assert.h>

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>

#include "ff_help.h"
#include "buffer_io.h"

static int _get_audio_volume(AVFormatContext *formatContext, int64_t start,
                             int64_t duration, float *max_volume,
                             float *mean_volume);

typedef struct VolDetectContext {
  /**
   * Number of samples at each PCM value.
   * histogram[0x8000 + i] is the number of samples at value i.
   * The extra element is there for symmetry.
   */
  uint64_t histogram[0x10001];
} VolDetectContext;

static int process_frame(VolDetectContext *vd, AVFrame *samples) {
  int nb_samples = samples->nb_samples;
  int nb_channels = samples->ch_layout.nb_channels;
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
  return 0;
}

#define MAX_DB 91

static inline double logdb(uint64_t v) {
  double d = v / (double)(0x8000 * 0x8000);
  if (!v)
    return MAX_DB;
  return -log10(d) * 10;
}

static void print_stats(VolDetectContext *vd) {
  int i, max_volume, shift;
  uint64_t nb_samples = 0, power = 0, nb_samples_shift = 0, sum = 0;
  uint64_t histdb[MAX_DB + 1] = {0};

  for (i = 0; i < 0x10000; i++)
    nb_samples += vd->histogram[i];

  printf("n_samples: %" PRId64 "\n", nb_samples);
  if (!nb_samples)
    return;

  /* If nb_samples > 1<<34, there is a risk of overflow in the
     multiplication or the sum: shift all histogram values to avoid that.
     The total number of samples must be recomputed to avoid rounding
     errors. */
  shift = av_log2(nb_samples >> 33);
  for (i = 0; i < 0x10000; i++) {
    nb_samples_shift += vd->histogram[i] >> shift;
    power += (i - 0x8000) * (i - 0x8000) * (vd->histogram[i] >> shift);
  }
  if (!nb_samples_shift)
    return;
  power = (power + nb_samples_shift / 2) / nb_samples_shift;
  assert(power <= 0x8000 * 0x8000);
  printf("mean_volume: %.1f dB\n", -logdb(power));

  max_volume = 0x8000;
  while (max_volume > 0 && !vd->histogram[0x8000 + max_volume] &&
         !vd->histogram[0x8000 - max_volume])
    max_volume--;
  printf("max_volume: %.1f dB\n", -logdb(max_volume * max_volume));

  for (i = 0; i < 0x10000; i++)
    histdb[(int)logdb((i - 0x8000) * (i - 0x8000))] += vd->histogram[i];
  for (i = 0; i <= MAX_DB && !histdb[i]; i++)
    ;
  for (; i <= MAX_DB && sum < nb_samples / 1000; i++) {
    printf("histogram_%ddb: %" PRId64 "\n", i, histdb[i]);
    sum += histdb[i];
  }
}

static void get_stats(VolDetectContext *vd, float *max_volume,
                      float *mean_volume) {
  int i, shift, maxvolume;
  uint64_t nb_samples = 0, power = 0, nb_samples_shift = 0, sum = 0;
  uint64_t histdb[MAX_DB + 1] = {0};

  for (i = 0; i < 0x10000; i++)
    nb_samples += vd->histogram[i];

  if (!nb_samples)
    return;

  /* If nb_samples > 1<<34, there is a risk of overflow in the
     multiplication or the sum: shift all histogram values to avoid that.
     The total number of samples must be recomputed to avoid rounding
     errors. */
  shift = av_log2(nb_samples >> 33);
  for (i = 0; i < 0x10000; i++) {
    nb_samples_shift += vd->histogram[i] >> shift;
    power += (i - 0x8000) * (i - 0x8000) * (vd->histogram[i] >> shift);
  }
  if (!nb_samples_shift)
    return;
  power = (power + nb_samples_shift / 2) / nb_samples_shift;
  assert(power <= 0x8000 * 0x8000);
  *mean_volume = -logdb(power);

  maxvolume = 0x8000;
  while (maxvolume > 0 && !vd->histogram[0x8000 + maxvolume] &&
         !vd->histogram[0x8000 - maxvolume])
    maxvolume--;
  *max_volume = -logdb(maxvolume * maxvolume);
}

static int build_filter_graph(AVFilterGraph *graph, AVStream *stream,
                              AVFilterContext **src_ctx,
                              AVFilterContext **sink_ctx) {
  int ret;
  char ch_layout[64];

  AVFilter *srcFilter, *outFilter, *aformatFilter;

  AVFilterContext *abuffer_ctx =
      avfilter_graph_alloc_filter(graph, avfilter_get_by_name("abuffer"), "in");
  AVFilterContext *abuffersink_ctx = avfilter_graph_alloc_filter(
      graph, avfilter_get_by_name("abuffersink"), "out");
  AVFilterContext *aformat_ctx = avfilter_graph_alloc_filter(
      graph, avfilter_get_by_name("aformat"), "aformat");
  if (aformat_ctx == NULL || abuffer_ctx == NULL || abuffersink_ctx == NULL) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  av_channel_layout_describe(&stream->codecpar->ch_layout, ch_layout,
                             sizeof(ch_layout));
  av_opt_set(abuffer_ctx, "channel_layout", ch_layout, AV_OPT_SEARCH_CHILDREN);
  av_opt_set(
      abuffer_ctx, "sample_fmt",
      av_get_sample_fmt_name((enum AVSampleFormat)stream->codecpar->format),
      AV_OPT_SEARCH_CHILDREN);
  av_opt_set_int(abuffer_ctx, "sample_rate", stream->codecpar->sample_rate,
                 AV_OPT_SEARCH_CHILDREN);

  ret = avfilter_init_str(abuffer_ctx, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not initialize the abuffer_ctx instance.\n");
    goto end;
  }

  ret = avfilter_init_str(abuffersink_ctx, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not initialize the abuffersink instance.\n");
    goto end;
  }

  av_opt_set(aformat_ctx, "sample_fmts", "s16|s16p", AV_OPT_SEARCH_CHILDREN);
  ret = avfilter_init_str(aformat_ctx, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not initialize the aformat_ctx instance.\n");
    goto end;
  }

  /* Connect the filters */
  ret = avfilter_link(abuffer_ctx, 0, aformat_ctx, 0);
  if (ret >= 0) {
    ret = avfilter_link(aformat_ctx, 0, abuffersink_ctx, 0);
  }
  if (ret < 0) {
    fprintf(stderr, "Error connecting filters\n");
    return ret;
  }

  ret = avfilter_graph_config(graph, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not configure volumedetect filter graph: %d\n", ret);
    return -1;
  }

  *src_ctx = abuffer_ctx;
  *sink_ctx = abuffersink_ctx;
end:
  return ret;
}

EXPORTED int ff_get_audio_volume_buffer(const uint8_t *buf, int buf_size,
                                        int64_t start, int64_t duration,
                                        float *max_volume, float *mean_volume) {
  AVFormatContext *fmt_ctx = NULL;
  AVIOContext *avio_ctx = NULL;
  uint8_t *avio_ctx_buffer = NULL;
  size_t avio_ctx_buffer_size = 4096;
  int ret = 0;
  struct buffer_data bd = {0};

  bd.ptr = (uint8_t *)buf;
  bd.size = buf_size;

  if (!(fmt_ctx = avformat_alloc_context())) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  avio_ctx_buffer = av_malloc(avio_ctx_buffer_size);
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

  ret = avformat_open_input(&fmt_ctx, NULL, NULL, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not open input\n");
    goto end;
  }

  ret = _get_audio_volume(fmt_ctx, start, duration, max_volume, mean_volume);

end:
  avformat_close_input(&fmt_ctx);
  if (avio_ctx)
    av_freep(&avio_ctx->buffer);
  avio_context_free(&avio_ctx);

  return ret;
}


EXPORTED int ff_get_audio_volume_callback(read_packet_t read_packet,
                                          int64_t start, int64_t duration,
                                          float *max_volume, float *mean_volume,
                                          void *ctx) {

  AVFormatContext *fmt_ctx = NULL;
  AVIOContext *avio_ctx = NULL;
  uint8_t *avio_ctx_buffer = NULL;
  size_t avio_ctx_buffer_size = 4096;
  int ret = 0;

  if (!(fmt_ctx = avformat_alloc_context())) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  avio_ctx_buffer = av_malloc(avio_ctx_buffer_size);
  if (!avio_ctx_buffer) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 0, ctx, read_packet, NULL, NULL);
  if (!avio_ctx) {
    ret = AVERROR(ENOMEM);
    goto end;
  }
  fmt_ctx->pb = avio_ctx;

  ret = avformat_open_input(&fmt_ctx, NULL, NULL, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not open input\n");
    goto end;
  }

  ret = _get_audio_volume(fmt_ctx, start, duration, max_volume, mean_volume);

end:
  avformat_close_input(&fmt_ctx);
  if (avio_ctx)
    av_freep(&avio_ctx->buffer);
  avio_context_free(&avio_ctx);

  return ret;

}


EXPORTED int ff_get_audio_volume(const char *filename, int64_t start,
                                 int64_t duration, float *max_volume,
                                 float *mean_volume) {
  AVFormatContext *formatContext = NULL;
  int ret;

  // Open the input file
  if (avformat_open_input(&formatContext, filename, NULL, NULL) != 0) {
    return AVERROR(EINVAL);
  }

  ret = _get_audio_volume(formatContext, start, duration, max_volume,
                          mean_volume);

  avformat_close_input(&formatContext);

  return ret;
}

int _get_audio_volume(AVFormatContext *formatContext, int64_t start,
                      int64_t duration, float *max_volume, float *mean_volume) {
  AVStream *stream = NULL;
  int stream_index = -1;
  AVFrame *frame = NULL;
  AVFilterGraph *graph = NULL;
  AVPacket *packet = av_packet_alloc();
  AVFilterContext *abuffer_ctx = NULL;
  AVFilterContext *abuffersink_ctx = NULL;
  VolDetectContext *vd = NULL;
  int64_t start_time = -1, end_time = -1;
  int ret;

  *max_volume = - MAX_DB;
  *mean_volume = - MAX_DB;

  // Find the audio stream
  ret = avformat_find_stream_info(formatContext, NULL);
  if (ret < 0) {
    goto end;
    return ret;
  }

  for (int i = 0; i < formatContext->nb_streams; i++) {
    if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      stream_index = i;
      stream = formatContext->streams[i];
      break;
    }
  }
  if (!stream) {
    ret = AVERROR(ENOENT);
    goto end;
  }

  // Allocate a frame
  frame = av_frame_alloc();
  if (frame == NULL) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  // Open the audio decoder
  const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (codec == NULL) {
    ret = AVERROR(ENOENT);
    goto end;
  }

  AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
  if (codec_ctx == NULL) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  if (avcodec_parameters_to_context(codec_ctx, stream->codecpar) < 0) {
    ret = AVERROR(EINVAL);
    goto end;
  }

  if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  graph = avfilter_graph_alloc();
  if (!graph) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  ret = build_filter_graph(graph, stream, &abuffer_ctx, &abuffersink_ctx);
  if (ret < 0) {
    fprintf(stderr, "Could not configure volumedetect filter graph: %d\n", ret);
    return -1;
  }

  vd = (VolDetectContext *)av_mallocz(sizeof(VolDetectContext));

  if (start > 0) {
    start_time =
        av_rescale_q(start * 1000000, AV_TIME_BASE_Q, stream->time_base);
  }
  if (duration > 0) {
    end_time = av_rescale_q(1000000 * (start + duration), AV_TIME_BASE_Q,
                            stream->time_base);
  }

  if (start_time > 0) {
    ret = av_seek_frame(formatContext, stream_index, start, 0);
    if (ret < 0) {
      ret = AVERROR(EINVAL);
      goto end;
    }
  }

  // Read packets from the input file
  while ((ret = av_read_frame(formatContext, packet)) >= 0) {
    if (packet->stream_index != stream->index) {
      av_packet_unref(packet);
      continue;
    }

    // Decode the packet
    ret = avcodec_send_packet(codec_ctx, packet);
    if (ret < 0) {
      goto end;
    }

    while (ret >= 0) {
      ret = avcodec_receive_frame(codec_ctx, frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      } else if (ret < 0) {
        goto end;
      }

      if (end_time > 0 && frame->pts >= end_time) {
        av_packet_unref(packet);
        goto end;
      }

      // Send the frame to the volumedetect filter
      ret = av_buffersrc_add_frame(abuffer_ctx, frame);
      if (ret < 0) {
        av_frame_unref(frame);
        break;
      }

      /* Get all the filtered output that is available. */
      while (1) {
        ret = av_buffersink_get_frame(abuffersink_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
          break;
        if (ret < 0)
          goto end;

        process_frame(vd, frame);
        av_frame_unref(frame);
      }
    }

    av_packet_unref(packet);
  }

  if (ret == AVERROR_EOF) {
    ret = 0;
  }

end:
  if (vd) {
    get_stats(vd, max_volume, mean_volume);
    av_free(vd);
  }

  avfilter_graph_free(&graph);
  av_packet_unref(packet);
  av_frame_free(&frame);

  return ret;
}
