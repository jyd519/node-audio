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

class EncryptWriter;

class Recorder : public Napi::ObjectWrap<Recorder> {
  public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    Recorder(const Napi::CallbackInfo& info);
    ~Recorder();
  private:
    bool hasError = false;
    std::string password;
    av::FormatContext ictx;
    int videoStream = -1;
    av::VideoDecoderContext vdec;
    int64_t count = 0;
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
    int64_t frames_dropped = 0;
    std::deque<av::VideoFrame> frames;
    std::mutex mtx_frames;
    std::condition_variable cv_frames;

    // Frame pool for memory reuse
    std::vector<std::unique_ptr<av::VideoFrame>> frame_pool;
    std::mutex mtx_pool;

    // Rescaler cache for different input formats
    std::map<std::tuple<int, int, AVPixelFormat>, av::VideoRescaler> rescaler_cache;
    std::mutex mtx_rescaler_cache;
    av::VideoRescaler* get_rescaler(av::VideoFrame &frame, av::VideoEncoderContext &encoder_ctx);

    av::VideoFrame get_frame_from_pool();
    void return_frame_to_pool(std::unique_ptr<av::VideoFrame> frame);

    void push(av::VideoFrame frame);
    bool pop(av::VideoFrame& frame);
    void drop_frames();
    void process_frames();
    void stop_thread();

    Napi::Value AddImage(const Napi::CallbackInfo &info);
    Napi::Value AddWebm(const Napi::CallbackInfo &info);
    Napi::Value Close(const Napi::CallbackInfo &info);
    Napi::Value FramesDropped(const Napi::CallbackInfo &info);
};

