#include <string>
#include <chrono>
#include <assert.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "avcpp/formatcontext.h"

#include "encrypt/joye_writer.h"

#ifdef _WIN32
inline std::wstring Utf8ToWide(const std::string &str) {
  std::wstring wstr;
  int nChars = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), 0, 0);
  if (nChars > 0) {
    wstr.resize(nChars);
    ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), wstr.data(), nChars);
  }
  return wstr;
}
#endif

class EncryptWriter : public av::CustomIO {
public:
  EncryptWriter(const std::string &name, const std::string &password)
      : _name(name), _password(password) {
#ifdef _WIN32
    _file = _wfsopen(Utf8ToWide(_name).c_str(), L"wb+", _SH_DENYWR);
#else
    _file = fopen(_name.c_str(), "wb+");
#endif
    if (_file) {
      writer = enc_writer_new(_file, password.c_str(), nullptr, 0);
    }
    last_flush = std::chrono::steady_clock::now();
  }
  ~EncryptWriter() {
    if (writer) {
      enc_writer_close(writer);
    }
    if (_file) {
      fclose(_file);
    }
  }
  int read(uint8_t * /*data*/, size_t /*size*/) override {
    // not supported
    assert(false);
    return -1;
  }
  int64_t seek(int64_t offset, int whence) override {
    if (!writer) {
      return AVERROR(EIO);
    }
    int rc = enc_writer_seek(writer, offset, whence);
    if (rc != 1) {
      return AVERROR(EIO);
    }
    return enc_writer_tell(writer);
  }
  int write(const uint8_t *data, size_t size) override {
    if (!writer) {
      return AVERROR(EIO);
    }
    auto now = std::chrono::steady_clock::now();
    int ret = (int)enc_writer_write(writer, (const void *)data, size);
    if (ret != size) {
      return AVERROR(EIO);
    }
    if (_file && std::chrono::duration_cast<std::chrono::seconds>(now - last_flush).count() >= 10) {
      fflush(_file);
      last_flush = now;
    }
    return ret;
  }
  int seekable() const override { return 0; }
  const char *name() const override { return _name.c_str(); }

private:
  std::string _name;
  std::string _password;
  FILE *_file = nullptr;
  enc_writer_ctx *writer = nullptr;
  std::chrono::steady_clock::time_point last_flush;
};
