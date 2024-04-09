#include "screen_capturer.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <libavutil/imgutils.h>
#include <libavutil/dict.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif

#include <thread>

inline constexpr auto str2fourcc(const char str[4]) {
  return ((str[3] << 24) + (str[2] << 16) + (str[1] << 8) + str[0]);
}

ScreenCapturer::ScreenCapturer(const std::string &path)
    : m_filePath(path), m_vIndex(-1), m_vOutIndex(-1), m_state(RecordState::NotStarted) {}

ScreenCapturer::~ScreenCapturer() { stop(); }

void ScreenCapturer::start() {
  if (m_state == RecordState::NotStarted) {
    m_state = RecordState::Started;
    m_collectFrameCnt = 0;
    m_encodeFrameCnt = 0;
    m_recordThread = std::make_unique<std::thread>(&ScreenCapturer::screenRecordThreadProc, this);
  } else if (m_state == RecordState::Paused) {
    m_state = RecordState::Started;
    m_cvNotPause.notify_one();
  }
}

void ScreenCapturer::pause() { m_state = RecordState::Paused; }

int64_t ScreenCapturer::stop() {
  if (m_state == RecordState::Paused) {
    m_cvNotPause.notify_one();
  }
  m_state = RecordState::Stopped;

  if (m_recordThread->joinable()) {
    m_cvNotEmpty.notify_one();
    m_recordThread->join();
  }

  return m_encodeFrameCnt;
}

inline int round16(int v) { return (v + 15) & ~15; }

int ScreenCapturer::openVideo() {
  int ret = -1;
  const AVInputFormat *ifmt = av_find_input_format("gdigrab");
  AVDictionary *options = nullptr;
  const AVCodec *decoder = nullptr;

  for (auto& v : m_opts) {
    av_dict_set(&options, v.first.c_str(), v.second.c_str(), 0); 
  }

  av_dict_set(&options, "framerate", std::to_string(m_fps).c_str(), 0);
  av_dict_set(&options, "probesize", (std::to_string(m_fps * 2) + "M").c_str(), 0);

  if (avformat_open_input(&m_vFmtCtx, "desktop", ifmt, &options) != 0) {
    av_log(nullptr, AV_LOG_ERROR, "Cant not open video input stream");
    return -1;
  }

  if (avformat_find_stream_info(m_vFmtCtx, nullptr) < 0) {
    av_log(nullptr, AV_LOG_ERROR, "Cant not find stream information");
    return -1;
  }

  for (uint32_t i = 0; i < m_vFmtCtx->nb_streams; ++i) {
    AVStream *stream = m_vFmtCtx->streams[i];
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      decoder = avcodec_find_decoder(stream->codecpar->codec_id);
      if (decoder == nullptr) {
        av_log(nullptr, AV_LOG_ERROR, "avcodec_find_decoder failed");
        return -1;
      }
      // Copy parameters from the video stream to codecCtx
      m_vDecodeCtx = avcodec_alloc_context3(decoder);
      if ((ret = avcodec_parameters_to_context(m_vDecodeCtx, stream->codecpar)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Video avcodec_parameters_to_context failed: %d", ret);
        return -1;
      }
      m_vIndex = i;
      break;
    }
  }

  m_vDecodeCtx->thread_count = std::thread::hardware_concurrency();
  m_vDecodeCtx->thread_type = FF_THREAD_SLICE;

  if (avcodec_open2(m_vDecodeCtx, decoder, nullptr) < 0) {
    av_log(nullptr, AV_LOG_ERROR, "avcodec_open2 failed");
    return -1;
  }

  if (m_width == 0) {
    m_width = m_vDecodeCtx->width;
  }
  if (m_height == 0) {
    m_height = m_vDecodeCtx->height;
  }

  av_log(nullptr, AV_LOG_INFO, "Capture resolution: %dx%d\n", m_width, m_height);

  m_video_width = m_width;
  m_video_height= m_height;
  auto ratio = m_height * 1.0 / m_width;
  if (m_width >= 4096 && m_height >= 2160) {
    m_video_width = 4096;
    m_video_height=round16(m_video_width * ratio);
  } if (m_width >= 3840 && m_height >= 2160) {
    m_video_width = 3840;
    m_video_height= round16(m_video_width * ratio);
  } else if (m_width >= 1920 && m_height >= 1080) {
    m_video_width = 1920;
    m_video_height= round16(m_video_width * ratio);
  }

  av_log(nullptr, AV_LOG_INFO, "Video resolution: %dx%d\n", m_video_width, m_video_height);

  m_swsCtx =
      sws_getContext(m_vDecodeCtx->width, m_vDecodeCtx->height, m_vDecodeCtx->pix_fmt, m_video_width,
                     m_video_height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

  return m_swsCtx != nullptr ? 0 : -1;
}

int ScreenCapturer::openOutput() {
  int ret = -1;
  AVStream *vStream = nullptr;
  std::string outFilePath = m_filePath;

  ret = avformat_alloc_output_context2(&m_oFmtCtx, nullptr, nullptr, outFilePath.c_str());
  if (ret < 0) {
    av_log(nullptr, AV_LOG_ERROR, "avformat_alloc_output_context2 failed");
    return -1;
  }

  const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!codec) {
    av_log(nullptr, AV_LOG_ERROR, "can not find h.264 encoder");
    return -1;
  }

  if (!m_title.empty()) {
    av_dict_set(&m_oFmtCtx->metadata, "title", m_title.c_str(), 0);
  }
  if (!m_comment.empty()) {
    av_dict_set(&m_oFmtCtx->metadata, "comment", m_comment.c_str(), 0);
  }

  if (m_vFmtCtx->streams[m_vIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    vStream = avformat_new_stream(m_oFmtCtx, nullptr);
    if (vStream == nullptr) {
      av_log(nullptr, AV_LOG_ERROR, "can not new stream for output\n");
      return -1;
    }

    m_vOutIndex = vStream->index;
    vStream->time_base = AVRational{1, m_fps};

    m_vEncodeCtx = avcodec_alloc_context3(codec);
    if (nullptr == m_vEncodeCtx) {
      av_log(nullptr, AV_LOG_ERROR, "avcodec_alloc_context3 failed");
      return -1;
    }

    // Set the encoding parameters
    setEncoderParams();

    // Find the video encoder
    const AVCodec *encoder = avcodec_find_encoder(m_vEncodeCtx->codec_id);
    if (encoder == nullptr) {
      av_log(nullptr, AV_LOG_ERROR, "Can not find the encoder, id: %d", (int)m_vEncodeCtx->codec_id);
      return -1;
    }

    // Open the video encoder
    ret = avcodec_open2(m_vEncodeCtx, encoder, &m_dict);
    if (ret < 0) {
      av_log(nullptr, AV_LOG_ERROR, "Can not open encoder id: %d error code: %d", (int)encoder->id, ret);
      return -1;
    }
    // Pass the parameters in codecCtx to the output stream
    ret = avcodec_parameters_from_context(vStream->codecpar, m_vEncodeCtx);
    if (ret < 0) {
      av_log(nullptr, AV_LOG_ERROR, "Output avcodec_parameters_from_context error code: %d", ret);
      return -1;
    }
  }

  // Open the output file
  if ((m_oFmtCtx->oformat->flags & AVFMT_NOFILE) == 0) {
    if (avio_open(&m_oFmtCtx->pb, outFilePath.c_str(), AVIO_FLAG_WRITE) < 0) {
      av_log(nullptr, AV_LOG_ERROR, "avio_open failed");
      return -1;
    }
  }

  // Write the file header
  if (!m_title.empty()) 
    av_dict_set(&m_oFmtCtx->metadata, "title", m_title.c_str(), 0);
  if (!m_comment.empty()) 
    av_dict_set(&m_oFmtCtx->metadata, "comment", m_comment.c_str(), 0);

  for (auto& v : m_opts) {
    av_dict_set(&m_dict, v.first.c_str(), v.second.c_str(), 0); 
  }

  if (avformat_write_header(m_oFmtCtx, &m_dict) < 0) {
    av_log(nullptr, AV_LOG_ERROR, "avformat_write_header failed");
    return -1;
  }

  return 0;
}

void ScreenCapturer::screenRecordThreadProc() {
  int ret = -1;
  bool done = false;
  int vFrameIndex = 0;

  avdevice_register_all();

  if (openVideo() < 0) {
    return;
  }
  if (openOutput() < 0) {
    return;
  }

  // Apply for fps*4 frame buffer
  if ((m_vFifo = av_fifo_alloc2(m_fps * 4, sizeof(AVFrame *), 0)) == nullptr) {
    av_log(nullptr, AV_LOG_ERROR, "av_fifo_alloc2 failed");
    return;
  }

  // Start the video data collection thread
  m_captureThread = std::make_unique<std::thread>(&ScreenCapturer::screenAcquireThreadProc, this);

  AVPacket *pkt = av_packet_alloc();
  int64_t ticket = av_gettime();
  while (true) {
    if (!done && m_captureStopped.load()) {
      done = true;
      if (m_captureThread->joinable()) {
        m_captureThread->join();
      }
    }

    {
      std::unique_lock<std::mutex> lk(m_mtx);
      m_cvNotEmpty.wait(lk, [this, &done] { return av_fifo_can_read(m_vFifo) > 0 || done; });
      if (av_fifo_can_read(m_vFifo) < 1 && done) {
        break;
      }
    }

    AVFrame *outFrame = NULL;
    av_fifo_read(m_vFifo, &outFrame, 1);
    m_cvNotFull.notify_one();

    // Set the video frame parameters
    outFrame->pts = vFrameIndex++;
    outFrame->format = m_vEncodeCtx->pix_fmt;
    outFrame->width = m_video_width;
    outFrame->height = m_video_height;
    outFrame->pict_type = AV_PICTURE_TYPE_NONE;
    ret = avcodec_send_frame(m_vEncodeCtx, outFrame);
    av_frame_free(&outFrame);
    if (ret != 0) {
      av_log(nullptr, AV_LOG_ERROR, "avcodec_send_frame failed: %d", ret);
      continue;
    }
    ret = avcodec_receive_packet(m_vEncodeCtx, pkt);
    if (ret != 0) {
      if (ret == AVERROR(EAGAIN)) {
        av_log(nullptr, AV_LOG_ERROR, "EAGAIN avcodec_receive_packet: %d", ret);
        continue;
      }
      av_log(nullptr, AV_LOG_ERROR, "avcodec_receive_packet failed: %d", ret);
      return;
    }

    av_packet_rescale_ts(pkt, m_vEncodeCtx->time_base, m_oFmtCtx->streams[m_vOutIndex]->time_base);
    pkt->duration = av_rescale_q(1, AVRational{1, m_fps}, m_vEncodeCtx->time_base);
    pkt->stream_index = m_vOutIndex;
    pkt->flags = 0;

    ret = av_interleaved_write_frame(m_oFmtCtx, pkt);
    if (ret == 0) {
      ++m_encodeFrameCnt;
      if ((av_gettime() - ticket) >= 2000000) {  // 2s
        ticket = av_gettime();
        avio_flush(m_oFmtCtx->pb);
      }
    } else {
      av_log(nullptr, AV_LOG_ERROR, "video av_interleaved_write_frame failed, ret:%d", ret);
    }
    av_packet_unref(pkt);
  }

  flushEncoder();
  av_write_trailer(m_oFmtCtx);
  av_packet_free(&pkt);
  release();
  av_log(nullptr, AV_LOG_INFO, "num of frames encoded: %" PRIi64, m_encodeFrameCnt);
}

void ScreenCapturer::screenAcquireThreadProc() {
  int ret = -1;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *oldFrame = av_frame_alloc();
  AVFrame *newFrame = av_frame_alloc();

  newFrame->format = m_vEncodeCtx->pix_fmt;
  newFrame->width = m_video_width;
  newFrame->height = m_video_height;
  ret = av_frame_get_buffer(newFrame, 0);
  if (ret < 0) {
    av_log(nullptr, AV_LOG_ERROR, "av_frame_get_buffer failed: %d", ret);
    m_captureStopped = true;
    return;
  }

  while (m_state != RecordState::Stopped) {
    if (m_state == RecordState::Paused) {
      std::unique_lock<std::mutex> lk(m_mtxPause);
      m_cvNotPause.wait(lk, [this] { return m_state != RecordState::Paused; });
    }

    if (av_read_frame(m_vFmtCtx, pkt) < 0) {
      av_log(nullptr, AV_LOG_ERROR, "video av_read_frame failed");
      continue;
    }
    if (pkt->stream_index != m_vIndex) {
      av_log(nullptr, AV_LOG_ERROR, "not a video packet from video input");
      av_packet_unref(pkt);
      continue;
    }

    ret = avcodec_send_packet(m_vDecodeCtx, pkt);
    if (ret != 0) {
      av_log(nullptr, AV_LOG_ERROR, "avcodec_send_packet failed, ret:%d", ret);
      av_packet_unref(pkt);
      continue;
    }
    ret = avcodec_receive_frame(m_vDecodeCtx, oldFrame);
    if (ret != 0) {
      av_log(nullptr, AV_LOG_ERROR, "avcodec_receive_frame failed, ret:%d", ret);
      av_packet_unref(pkt);
      continue;
    }
    av_packet_unref(pkt);

    ++m_collectFrameCnt;

    sws_scale_frame(m_swsCtx, newFrame, oldFrame);
    newFrame->time_base = AV_TIME_BASE_Q;
    newFrame->pts = oldFrame->pts;
    newFrame->pkt_dts = oldFrame->pkt_dts;
    av_frame_unref(oldFrame);

    {
      std::unique_lock<std::mutex> lk(m_mtx);
      m_cvNotFull.wait(lk, [this] { return av_fifo_can_write(m_vFifo) > 0; });
    }

    AVFrame *clone = av_frame_clone(newFrame);
    ret = av_fifo_write(m_vFifo, &clone, 1);
    if (ret < 0) {
      av_frame_free(&clone);
      break;
    }

    m_cvNotEmpty.notify_one();
  }

  flushDecoder();

  av_frame_free(&oldFrame);
  av_frame_free(&newFrame);
  av_packet_free(&pkt);
  m_captureStopped = true;
}

void ScreenCapturer::setEncoderParams() {
  if ((m_quality < 1) || (m_quality > 50)) {
    m_quality = 5;
  }

  m_vEncodeCtx->width = m_video_width;
  m_vEncodeCtx->height = m_video_height;
  m_vEncodeCtx->codec_type = AVMEDIA_TYPE_VIDEO;
  m_vEncodeCtx->time_base.num = 1;
  m_vEncodeCtx->time_base.den = m_fps;
  if (m_gop > 0) 
    m_vEncodeCtx->gop_size = m_gop;
  m_vEncodeCtx->max_b_frames = 2;
  m_vEncodeCtx->qmin = m_quality - (m_quality > 1 ? 1 : 0);
  m_vEncodeCtx->qmax = m_quality + 1;
  m_vEncodeCtx->pix_fmt = AV_PIX_FMT_YUV420P;
  if (m_oFmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
    m_vEncodeCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  av_dict_set(&m_dict, "preset", "slow", 0);
  av_dict_set(&m_dict, "rc_mode", "quality", 0);
}

void ScreenCapturer::flushDecoder() {
  int ret = -1;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *oldFrame = av_frame_alloc();
  AVFrame *newFrame = av_frame_alloc();
  newFrame->format = m_vEncodeCtx->pix_fmt;
  newFrame->width = m_video_width;
  newFrame->height = m_video_height;
  av_frame_get_buffer(newFrame, 0);

  ret = avcodec_send_packet(m_vDecodeCtx, nullptr);
  av_log(nullptr, AV_LOG_DEBUG, "flush avcodec_send_packet, ret: %d", ret);
  while (ret >= 0) {
    ret = avcodec_receive_frame(m_vDecodeCtx, oldFrame);
    if (ret < 0) {
      av_packet_unref(pkt);
      if (ret == AVERROR(EAGAIN)) {
        av_log(nullptr, AV_LOG_DEBUG, "flush EAGAIN avcodec_receive_frame");
        continue;
      } else if (ret == AVERROR_EOF) {
        av_log(nullptr, AV_LOG_DEBUG, "flush video decoder finished");
        break;
      }
      av_log(nullptr, AV_LOG_ERROR, "flush avcodec_receive_frame, ret: %d", ret);
      return;
    }
    ++m_collectFrameCnt;
    sws_scale(m_swsCtx, (const uint8_t *const *)oldFrame->data, oldFrame->linesize, 0,
              m_vEncodeCtx->height, newFrame->data, newFrame->linesize);

    {
      std::unique_lock<std::mutex> lk(m_mtx);
      m_cvNotFull.wait(lk, [this] { return av_fifo_can_write(m_vFifo) >= 1; });
    }

    AVFrame *clone = av_frame_clone(newFrame);
    ret = av_fifo_write(m_vFifo, clone, 1);
    if (ret < 0) {
      av_log(nullptr, AV_LOG_ERROR, "flush av_fifo_write, ret: %d", ret);
      av_frame_free(&clone);
      break;
    }

    m_cvNotEmpty.notify_one();
  }
  av_packet_free(&pkt);
  av_log(nullptr, AV_LOG_DEBUG, "collect frame count: %" PRIi64, m_collectFrameCnt);
}

void ScreenCapturer::flushEncoder() {
  int ret = -11;
  AVPacket *pkt = av_packet_alloc();
  ret = avcodec_send_frame(m_vEncodeCtx, nullptr);
  if (ret < 0) {
    av_log(nullptr, AV_LOG_ERROR, "flush: avcodec_send_frame error, ret: %d", ret);
    return;
  }
  while (ret >= 0) {
    ret = avcodec_receive_packet(m_vEncodeCtx, pkt);
    if (ret < 0) {
      if (ret == AVERROR(EAGAIN)) {
        av_log(nullptr, AV_LOG_DEBUG, "flush: EAGAIN avcodec_receive_packet");
        continue;
      } else if (ret == AVERROR_EOF) {
        av_log(nullptr, AV_LOG_DEBUG, "flush: video encoder finished");
        break;
      }
      av_log(nullptr, AV_LOG_ERROR, "flush: avcodec_receive_packet failed %d", ret);
      return;
    }

    pkt->stream_index = m_vOutIndex;
    av_packet_rescale_ts(pkt, m_vEncodeCtx->time_base, m_oFmtCtx->streams[m_vOutIndex]->time_base);
    pkt->duration = av_rescale_q(1, AVRational{1, m_fps}, m_vEncodeCtx->time_base);
    pkt->flags = 0;

    ret = av_interleaved_write_frame(m_oFmtCtx, pkt);
    if (ret == 0) {
      ++m_encodeFrameCnt;
      av_log(nullptr, AV_LOG_DEBUG, "flush: Write video packet id: %" PRIi64, m_encodeFrameCnt);
    } else {
      av_log(nullptr, AV_LOG_ERROR, "flush: av_interleaved_write_frame failed: %d", ret);
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
}

void ScreenCapturer::release() {
  if (m_vDecodeCtx != nullptr) {
    avcodec_free_context(&m_vDecodeCtx);
  }
  if (m_vEncodeCtx != nullptr) {
    avcodec_free_context(&m_vEncodeCtx);
  }
  if (m_vFifo != nullptr) {
    av_fifo_freep2(&m_vFifo);
  }
  if (m_vFmtCtx != nullptr) {
    avformat_close_input(&m_vFmtCtx);
  }
  if (m_oFmtCtx != nullptr) {
    avformat_close_input(&m_oFmtCtx);
  }
  if (m_dict) {
    av_dict_free(&m_dict);
  }
  if (m_swsCtx) {
    sws_freeContext(m_swsCtx);
    m_swsCtx = nullptr;
  }
}
