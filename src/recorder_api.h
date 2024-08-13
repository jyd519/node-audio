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
#include "codec.h"
#include "codeccontext.h"

#include <thread>
#include <deque>
#include <condition_variable>

class Recorder : public Napi::ObjectWrap<Recorder> {
  public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    Recorder(const Napi::CallbackInfo& info);
    ~Recorder();
  private:
    bool hasError = false;
    av::FormatContext ictx;
    int videoStream = -1;
    av::VideoDecoderContext vdec;
    av::Stream vst;
    int count = 0;
    int fps = 15;
    int width  = 800;
    int height = 600;

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

    void push(av::VideoFrame frame);
    bool pop(av::VideoFrame& frame);
    void drop_frames();
    void process_frames();
    void stop_thread();

    Napi::Value AddImage(const Napi::CallbackInfo &info);
    Napi::Value Close(const Napi::CallbackInfo &info);
    Napi::Value FramesDropped(const Napi::CallbackInfo &info);
};

