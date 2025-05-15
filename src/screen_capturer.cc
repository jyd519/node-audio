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

/**
 * Convert a 4-character string to a FourCC code
 * @param str 4-character string
 * @return FourCC code as uint32_t
 */
inline constexpr uint32_t str2fourcc(const char str[4]) {
  return ((static_cast<uint32_t>(str[3]) << 24) + 
          (static_cast<uint32_t>(str[2]) << 16) + 
          (static_cast<uint32_t>(str[1]) << 8) + 
           static_cast<uint32_t>(str[0]));
}

/**
 * Constructor initializes the screen capturer with output file path
 * @param path Path to the output video file
 */
ScreenCapturer::ScreenCapturer(const std::string &path)
    : m_filePath(path), 
      m_vIndex(-1), 
      m_vOutIndex(-1), 
      m_state(RecordState::NotStarted),
      m_captureStopped(false) {}

/**
 * Destructor ensures recording is stopped and resources are released
 */
ScreenCapturer::~ScreenCapturer() { 
  stop(); 
}

/**
 * Start or resume screen recording
 */
void ScreenCapturer::start() {
  if (m_state == RecordState::NotStarted) {
    // Initialize counters and start recording thread
    m_collectFrameCnt = 0;
    m_encodeFrameCnt = 0;
    m_captureStopped = false;
    m_state = RecordState::Started;
    
    try {
      m_recordThread = std::make_unique<std::thread>(&ScreenCapturer::screenRecordThreadProc, this);
    } catch (const std::exception& e) {
      av_log(nullptr, AV_LOG_ERROR, "Failed to start recording thread: %s", e.what());
      m_state = RecordState::NotStarted;
    }
  } else if (m_state == RecordState::Paused) {
    // Resume from paused state
    m_state = RecordState::Started;
    m_cvNotPause.notify_one();
  }
}

/**
 * Pause screen recording
 */
void ScreenCapturer::pause() { 
  if (m_state == RecordState::Started) {
    m_state = RecordState::Paused; 
  }
}

/**
 * Stop screen recording and clean up resources
 * @return Number of frames encoded
 */
int64_t ScreenCapturer::stop() {
  // If we're in paused state, notify the thread to resume before stopping
  if (m_state == RecordState::Paused) {
    m_cvNotPause.notify_one();
  }
  
  // Only proceed if we're actually recording
  if (m_state != RecordState::NotStarted && m_state != RecordState::Stopped) {
    m_state = RecordState::Stopped;

    // Wait for recording thread to finish
    if (m_recordThread && m_recordThread->joinable()) {
      m_cvNotEmpty.notify_one();
      m_recordThread->join();
    }
  }

  return m_encodeFrameCnt;
}

/**
 * Round up to the nearest multiple of 16
 * @param v Value to round
 * @return Rounded value
 */
inline int round16(int v) { 
  return (v + 15) & ~15; 
}

int ScreenCapturer::openVideo() {
  int ret = -1;
  
  // Find the GDI screen capture input format
  const AVInputFormat *ifmt = av_find_input_format("gdigrab");
  if (!ifmt) {
    av_log(nullptr, AV_LOG_ERROR, "Could not find gdigrab input format");
    return -1;
  }
  
  AVDictionary *options = nullptr;
  const AVCodec *decoder = nullptr;

  // Apply user-defined options
  for (const auto& v : m_opts) {
    av_dict_set(&options, v.first.c_str(), v.second.c_str(), 0);
  }

  // Set capture framerate and probesize
  av_dict_set(&options, "framerate", std::to_string(m_fps).c_str(), 0);
  av_dict_set(&options, "probesize", (std::to_string(m_fps * 2) + "M").c_str(), 0);

  // Open the input device
  ret = avformat_open_input(&m_vFmtCtx, "desktop", ifmt, &options);
  if (ret != 0) {
    char error[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
    av_log(nullptr, AV_LOG_ERROR, "Cannot open video input stream: %s", error);
    av_dict_free(&options);
    return -1;
  }
  av_dict_free(&options);

  // Find stream information
  ret = avformat_find_stream_info(m_vFmtCtx, nullptr);
  if (ret < 0) {
    char error[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
    av_log(nullptr, AV_LOG_ERROR, "Cannot find stream information: %s", error);
    return -1;
  }

  // Find the video stream and its decoder
  bool foundVideoStream = false;
  for (uint32_t i = 0; i < m_vFmtCtx->nb_streams; ++i) {
    AVStream *stream = m_vFmtCtx->streams[i];
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      decoder = avcodec_find_decoder(stream->codecpar->codec_id);
      if (!decoder) {
        av_log(nullptr, AV_LOG_ERROR, "Cannot find suitable decoder for codec id: %d", stream->codecpar->codec_id);
        return -1;
      }
      
      // Allocate and initialize decoder context
      m_vDecodeCtx = avcodec_alloc_context3(decoder);
      if (!m_vDecodeCtx) {
        av_log(nullptr, AV_LOG_ERROR, "Failed to allocate decoder context");
        return -1;
      }
      
      // Copy parameters from the video stream to decoder context
      ret = avcodec_parameters_to_context(m_vDecodeCtx, stream->codecpar);
      if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
        av_log(nullptr, AV_LOG_ERROR, "Video avcodec_parameters_to_context failed: %s", error);
        return -1;
      }
      
      m_vIndex = i;
      foundVideoStream = true;
      break;
    }
  }
  
  if (!foundVideoStream) {
    av_log(nullptr, AV_LOG_ERROR, "No video stream found");
    return -1;
  }

  // Configure decoder for multithreading
  m_vDecodeCtx->thread_count = std::thread::hardware_concurrency();
  m_vDecodeCtx->thread_type = FF_THREAD_SLICE;

  // Open the decoder
  ret = avcodec_open2(m_vDecodeCtx, decoder, nullptr);
  if (ret < 0) {
    char error[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(ret, error, AV_ERROR_MAX_STRING_SIZE);
    av_log(nullptr, AV_LOG_ERROR, "Failed to open decoder: %s", error);
    return -1;
  }

  // Use source dimensions if not specified
  if (m_width == 0) {
    m_width = m_vDecodeCtx->width;
  }
  if (m_height == 0) {
    m_height = m_vDecodeCtx->height;
  }

  av_log(nullptr, AV_LOG_INFO, "Capture resolution: %dx%d", m_width, m_height);

  // Determine output video dimensions based on input resolution
  m_video_width = m_width;
  m_video_height = m_height;
  double ratio = static_cast<double>(m_height) / m_width;
  
  // Scale down high resolution captures
  if (m_width >= 4096 && m_height >= 2160) {
    m_video_width = 4096;
    m_video_height = round16(static_cast<int>(m_video_width * ratio));
  } else if (m_width >= 3840 && m_height >= 2160) {
    m_video_width = 3840;
    m_video_height = round16(static_cast<int>(m_video_width * ratio));
  } else if (m_width >= 1920 && m_height >= 1080) {
    m_video_width = 1920;
    m_video_height = round16(static_cast<int>(m_video_width * ratio));
  }

  av_log(nullptr, AV_LOG_INFO, "Output video resolution: %dx%d", m_video_width, m_video_height);

  // Initialize the scaling context for format conversion and resizing
  m_swsCtx = sws_getContext(
      m_vDecodeCtx->width,
      m_vDecodeCtx->height,
      m_vDecodeCtx->pix_fmt,
      m_video_width,
      m_video_height,
      AV_PIX_FMT_YUV420P,
      SWS_FAST_BILINEAR,
      nullptr,
      nullptr,
      nullptr
  );

  if (!m_swsCtx) {
    av_log(nullptr, AV_LOG_ERROR, "Failed to initialize scaling context");
    return -1;
  }
  
  return 0;
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
#if LIBAVCODEC_VERSION_MAJOR > 58
  if ((m_vFifo = av_fifo_alloc2(m_fps * 4, sizeof(AVFrame *), 0)) == nullptr) {
    av_log(nullptr, AV_LOG_ERROR, "av_fifo_alloc2 failed");
    return;
  }
#else
  if ((m_vFifo = av_fifo_alloc(m_fps * 4 * sizeof(AVFrame *))) == nullptr) {
    av_log(nullptr, AV_LOG_ERROR, "av_fifo_alloc2 failed");
    return;
  }
#endif
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

#if LIBAVCODEC_VERSION_MAJOR > 58
      m_cvNotEmpty.wait(lk, [this, &done] { return av_fifo_can_read(m_vFifo) > 0 || done; });
      if (av_fifo_can_read(m_vFifo) < 1 && done) {
        break;
      }
#else
      m_cvNotEmpty.wait(lk, [this, &done] { return av_fifo_size(m_vFifo) > 0 || done; });
      if (av_fifo_size(m_vFifo) < 1 && done) {
        break;
      }
#endif
    }

    AVFrame *outFrame = NULL;
#if LIBAVCODEC_VERSION_MAJOR > 58
    av_fifo_read(m_vFifo, &outFrame, 1);
#else
    if (av_fifo_size(m_vFifo) > sizeof(AVFrame*)) {
      uint8_t *data = NULL;
      av_fifo_generic_read(m_vFifo, &data, sizeof(AVFrame *), NULL);
      outFrame = *(AVFrame **)data;
    }
#endif
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


#if LIBAVCODEC_VERSION_MAJOR <= 58
bool avscale_frame(struct SwsContext *swsCtx, AVFrame* oldFrame, AVFrame* newFrame) {
  int src_w = oldFrame->width;
  int src_h = oldFrame->height;
  int src_format = oldFrame->format;

  int dst_w = newFrame->width;
  int dst_h = newFrame->height;
  int dst_format = newFrame->format;

  int ret_sws = sws_scale(swsCtx, oldFrame->data, oldFrame->linesize, 0, src_h,
                          newFrame->data, newFrame->linesize);
  if (ret_sws < 0) {
      // 处理sws_scale失败的情况，比如打印错误信息等
      fprintf(stderr, "sws_scale failed: %d\n", ret_sws);
      av_log(nullptr, AV_LOG_ERROR, "sws_scale failed: %d", ret_sws);
      return false;
  }
  return true;
}
#endif

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

#if LIBAVCODEC_VERSION_MAJOR > 58
    sws_scale_frame(m_swsCtx, newFrame, oldFrame);
    newFrame->time_base = AV_TIME_BASE_Q;
#else
    avscale_frame(m_swsCtx, newFrame, oldFrame);
#endif
    newFrame->pts = oldFrame->pts;
    newFrame->pkt_dts = oldFrame->pkt_dts;
    av_frame_unref(oldFrame);

    {
      std::unique_lock<std::mutex> lk(m_mtx);

#if LIBAVCODEC_VERSION_MAJOR > 58
      m_cvNotFull.wait(lk, [this] { return av_fifo_can_write(m_vFifo) > 0; });
#else
      m_cvNotFull.wait(lk, [this] { return av_fifo_space(m_vFifo) > 0; });
#endif
    }

    AVFrame *clone = av_frame_clone(newFrame);
#if LIBAVCODEC_VERSION_MAJOR > 58
    ret = av_fifo_write(m_vFifo, &clone, 1);
#else
    ret = av_fifo_generic_write(m_vFifo, &clone, sizeof(AVFrame*), nullptr);
#endif
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

/**
 * Configure encoder parameters for video output
 */
void ScreenCapturer::setEncoderParams() {
  // Validate quality parameter (1-50 range, lower is better quality)
  if ((m_quality < 1) || (m_quality > 50)) {
    av_log(nullptr, AV_LOG_WARNING, "Invalid quality value %d, using default of 5", m_quality);
    m_quality = 5;
  }

  // Set basic video parameters
  m_vEncodeCtx->width = m_video_width;
  m_vEncodeCtx->height = m_video_height;
  m_vEncodeCtx->codec_type = AVMEDIA_TYPE_VIDEO;
  
  // Set time base (1/fps)
  m_vEncodeCtx->time_base.num = 1;
  m_vEncodeCtx->time_base.den = m_fps;
  
  // Set GOP (Group of Pictures) size if specified
  if (m_gop > 0) {
    m_vEncodeCtx->gop_size = m_gop;
  } else {
    // Default GOP size to 2 seconds of frames
    m_vEncodeCtx->gop_size = m_fps * 2;
  }
  
  // Configure quality parameters
  m_vEncodeCtx->max_b_frames = 2;  // Use up to 2 B-frames for better compression
  m_vEncodeCtx->qmin = m_quality - (m_quality > 1 ? 1 : 0);
  m_vEncodeCtx->qmax = m_quality + 1;
  
  // Set pixel format to YUV420P (widely compatible)
  m_vEncodeCtx->pix_fmt = AV_PIX_FMT_YUV420P;
  
  // Set global header flag if needed by the output format
  if (m_oFmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
    m_vEncodeCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  
  // Set encoder preset and rate control mode
  av_dict_set(&m_dict, "preset", "slow", 0);  // Slow preset for better quality
  av_dict_set(&m_dict, "rc_mode", "quality", 0);  // Quality-based rate control
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
#if LIBAVCODEC_VERSION_MAJOR > 58
      m_cvNotFull.wait(lk, [this] { return av_fifo_can_write(m_vFifo) > 0; });
#else
      m_cvNotFull.wait(lk, [this] { return av_fifo_space(m_vFifo) > 0; });
#endif

    }

    AVFrame *clone = av_frame_clone(newFrame);

#if LIBAVCODEC_VERSION_MAJOR > 58
    ret = av_fifo_write(m_vFifo, &clone, 1);
#else
    ret = av_fifo_generic_write(m_vFifo, &clone, sizeof(AVFrame*), nullptr);
#endif
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

#if LIBAVCODEC_VERSION_MAJOR > 58
    av_fifo_freep2(&m_vFifo);
#else
    av_fifo_freep(&m_vFifo);
#endif
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
