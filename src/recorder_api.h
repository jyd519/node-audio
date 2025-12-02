#include <napi.h>

#include "av.h"
#include "avcpp/ffmpeg.h"
#include "codec.h"
#include "packet.h"
#include "videorescaler.h"
#include "audioresampler.h"
#include "avutils.h"

// API2
#include "format.h"
#include "formatcontext.h"
#include "codeccontext.h"

#include <thread>
#include <deque>
#include <condition_variable>
#include <vector>
#include <map>
#include <tuple>
#include <chrono>

class EncryptWriter;

class Recorder : public Napi::ObjectWrap<Recorder> {
  public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    Recorder(const Napi::CallbackInfo& info);
    ~Recorder();

    void push(av::VideoFrame frame);
    bool getHasError() const { return hasError; }
    void setHasError(bool error) { hasError = error; }
  private:
    bool hasError = false;
    std::string password;
    av::FormatContext ictx;
    int videoStream = -1;
    av::VideoDecoderContext vdec;
    std::atomic<int64_t> count = 0;
    int fps = 15;
    int width  = 800;
    int height = 600;
    std::unique_ptr<EncryptWriter> owriter;

    av::OutputFormat ofrmt;
    av::FormatContext octx;
    av::VideoEncoderContext encoder;

    av::VideoRescaler rescaler;
    std::thread thrd;
    bool done;
    std::deque<av::VideoFrame> frames;
    std::mutex mtx_frames;
    std::condition_variable cv_frames;
    std::condition_variable cv_queue;

    // Flush timing control
    std::chrono::steady_clock::time_point last_flush_time;
    static constexpr std::chrono::seconds FLUSH_INTERVAL{2};

    // Rescaler cache for different input formats
    std::map<std::tuple<int, int, AVPixelFormat>, av::VideoRescaler> rescaler_cache;
    std::mutex mtx_rescaler_cache;
    av::VideoRescaler* get_rescaler(av::VideoFrame &frame, av::VideoEncoderContext &encoder_ctx);

    bool pop(av::VideoFrame& frame);
    void drop_frames();
    void process_frames();
    void stop_thread();

    Napi::Value AddImage(const Napi::CallbackInfo &info);
    Napi::Value AddWebm(const Napi::CallbackInfo &info);
    Napi::Value Close(const Napi::CallbackInfo &info);
    Napi::Value FramesAdded(const Napi::CallbackInfo &info);
};

