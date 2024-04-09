#ifndef SCREEN_CAPTURER_H
#define SCREEN_CAPTURER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <map>

#ifdef __cplusplus
extern "C" {
#endif
struct AVFormatContext;
struct AVCodecContext;
struct AVCodec;
struct AVFifo;
struct AVAudioFifo;
struct AVFrame;
struct SwsContext;
struct SwrContext;
struct AVDictionary;
#ifdef __cplusplus
}
#endif

class ScreenCapturer {
public:
  enum RecordState {
    NotStarted,
    Started,
    Paused,
    Stopped,
    Unknown,
  };

  ScreenCapturer(const std::string &path);
  ~ScreenCapturer();

  // Initialize the video parameters
  void set_fps(int fps) { m_fps = fps; }
  void set_size(int w, int h) {
    m_width = w;
    m_height = h;
  }
  void set_quality(int quality) { m_quality = quality; }
  void set_title(const std::string &title) { m_title = title; }
  void set_comment(const std::string &comment) { m_comment = comment; }
  void set_option(const std::string &key, const std::string &value) { m_opts[key] = value; }
  void set_gop(int size) { m_gop = size; }

  void start();
  void pause();
  int64_t stop();

private:
  // Read video frames from fifo, encoding write output stream, generate files
  void screenRecordThreadProc();
  // Read the frame from the video input stream
  void screenAcquireThreadProc();

  int openVideo();
  int openOutput();
  void setEncoderParams();
  void flushDecoder();
  void flushEncoder();
  void release();

private:
  std::string m_filePath;
  std::string m_title;
  std::string m_comment;
  int m_quality = 10;
  int m_width = 0;
  int m_height = 0;
  int m_video_width = 0;
  int m_video_height = 0;
  int m_fps = 25;
  int m_gop = 0;
  std::map<std::string, std::string> m_opts;

  std::unique_ptr<std::thread> m_recordThread = nullptr;
  std::unique_ptr<std::thread> m_captureThread = nullptr;

  int m_vIndex;    // Input video stream index
  int m_vOutIndex; // output video stream index
  AVFormatContext *m_vFmtCtx = nullptr;
  AVFormatContext *m_oFmtCtx = nullptr;
  AVCodecContext *m_vDecodeCtx = nullptr;
  AVCodecContext *m_vEncodeCtx = nullptr;
  AVDictionary *m_dict = nullptr;
  SwsContext *m_swsCtx = nullptr;
  AVFifo *m_vFifo = nullptr;
  RecordState m_state;
  int64_t m_collectFrameCnt = 0; // Number of frames collected
  int64_t m_encodeFrameCnt = 0;  // coded frame number

  // The encoding speed is generally slower than the acquisition speed, so you
  // can remove m_cvNotEmpty
  std::condition_variable m_cvNotFull;
  std::condition_variable m_cvNotEmpty;
  std::mutex m_mtx;
  std::condition_variable m_cvNotPause;
  std::mutex m_mtxPause;
  std::atomic<bool> m_captureStopped;
};

#endif // SCREEN_CAPTURER_H
