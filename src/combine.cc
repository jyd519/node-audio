#include "combine.h"

#include <sstream>
#include <algorithm>

extern "C" {
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include "buffer_io.h"

int ff_combine(std::vector<std::string> &inputs, const std::string &out_filename) {
  AVIOContext *avio_ctx = NULL;
  uint8_t *avio_ctx_buffer = NULL;
  size_t avio_ctx_buffer_size = 4096;
  struct buffer_data bd = {0};
  std::stringstream ss;

  std::for_each(inputs.begin(), inputs.end(),
                [&ss](const std::string &s) { ss << "file '" << s << "'\n"; });
  std::string list = ss.str();
  bd.ptr = (uint8_t *)list.c_str();
  bd.size = list.size();

  const AVOutputFormat *ofmt = NULL;
  AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
  AVDictionary *options = NULL;
  AVPacket *pkt = NULL;
  int ret, i;
  int stream_index = 0;
  int *stream_mapping = NULL;
  int stream_mapping_size = 0;

  pkt = av_packet_alloc();
  if (!pkt) {
    fprintf(stderr, "combine: Could not allocate AVPacket\n");
    return 1;
  }

  if (!(ifmt_ctx = avformat_alloc_context())) {
    fprintf(stderr, "combine: Could not allocate input AVFormatContext\n");
    ret = AVERROR(ENOMEM);
    goto end;
  }

  avio_ctx_buffer = (uint8_t *)av_malloc(avio_ctx_buffer_size);
  if (!avio_ctx_buffer) {
    ret = AVERROR(ENOMEM);
    goto end;
  }
  avio_ctx =
      avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 0, &bd, &read_packet, NULL, NULL);
  if (!avio_ctx) {
    ret = AVERROR(ENOMEM);
    goto end;
  }
  ifmt_ctx->pb = avio_ctx;
  av_dict_set(&options, "safe", "0", 0);
  if ((ret = avformat_open_input(&ifmt_ctx, 0, av_find_input_format("concat"), &options)) < 0) {
    fprintf(stderr, "combine: Could not open input file\n");
    goto end;
  }

  if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
    fprintf(stderr, "combine: Failed to retrieve input stream information");
    goto end;
  }

  av_dump_format(ifmt_ctx, 0, "inputs", 0);

  avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename.c_str());
  if (!ofmt_ctx) {
    fprintf(stderr, "combine: Could not create output context\n");
    ret = AVERROR_UNKNOWN;
    goto end;
  }

  stream_mapping_size = ifmt_ctx->nb_streams;
  stream_mapping = (int *)av_calloc(stream_mapping_size, sizeof(*stream_mapping));
  if (!stream_mapping) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  ofmt = ofmt_ctx->oformat;
  for (i = 0; i < ifmt_ctx->nb_streams; i++) {
    AVStream *out_stream;
    AVStream *in_stream = ifmt_ctx->streams[i];
    AVCodecParameters *in_codecpar = in_stream->codecpar;

    // 除了音频，视频，字幕流, 其他流都不复制
    if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
        in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
        in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
      stream_mapping[i] = -1;
      continue;
    }

    stream_mapping[i] = stream_index++;

    out_stream = avformat_new_stream(ofmt_ctx, 0);
    if (!out_stream) {
      fprintf(stderr, "combine: Failed allocating output stream\n");
      ret = AVERROR_UNKNOWN;
      goto end;
    }

    ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
    if (ret < 0) {
      fprintf(stderr, "combine: Failed to copy codec parameters\n");
      goto end;
    }
    out_stream->codecpar->codec_tag = 0;
  }
  av_dump_format(ofmt_ctx, 0, out_filename.c_str(), 1);

  if (!(ofmt->flags & AVFMT_NOFILE)) {
    ret = avio_open(&ofmt_ctx->pb, out_filename.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
      fprintf(stderr, "combine: Could not open output file '%s'", out_filename.c_str());
      goto end;
    }
  }

  av_dict_set(&options, "movflags", "faststart", 0);
  ret = avformat_write_header(ofmt_ctx, &options);
  if (ret < 0) {
    fprintf(stderr, "combine: Error occurred when opening output file\n");
    goto end;
  }

  while (1) {
    AVStream *in_stream, *out_stream;

    ret = av_read_frame(ifmt_ctx, pkt);
    if (ret < 0) {
      if (ret == AVERROR_EOF) {
        ret = 0;
      }
      break;
    }

    in_stream = ifmt_ctx->streams[pkt->stream_index];
    if (pkt->stream_index >= stream_mapping_size || stream_mapping[pkt->stream_index] < 0) {
      av_packet_unref(pkt);
      continue;
    }

    pkt->stream_index = stream_mapping[pkt->stream_index];
    out_stream = ofmt_ctx->streams[pkt->stream_index];

    /* copy packet */
    av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
    pkt->pos = -1;

    ret = av_interleaved_write_frame(ofmt_ctx, pkt);
    /* pkt is now blank (av_interleaved_write_frame() takes ownership of
     * its contents and resets pkt), so that no unreferencing is necessary.
     * This would be different if one used av_write_frame(). */
    if (ret < 0) {
      fprintf(stderr, "combine: error muxing packet\n");
      break;
    }
  }

  // 写入文件尾部
  ret = av_write_trailer(ofmt_ctx);

end:
  av_packet_free(&pkt);

  av_dict_free(&options);
  avformat_close_input(&ifmt_ctx);

  /* close output */
  if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
    avio_closep(&ofmt_ctx->pb);
  avformat_free_context(ofmt_ctx);

  av_freep(&stream_mapping);

  if (ret < 0 && ret != AVERROR_EOF) {
    fprintf(stderr, "combine: error occurred: %d", ret);
    return 1;
  }

  return ret;
}
