#ifndef _ENC_READER_H_
#define _ENC_READER_H_

#include <string>
#include <assert.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <avcpp/format.h>
#include <avcpp/ffmpeg.h>
#include <avcpp/formatcontext.h>

#include "encrypt/joye_reader.h"

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

inline bool is_enc_file(const char* name) {
  FILE *file = NULL;
#ifdef _WIN32
  file = _wfsopen(Utf8ToWide(name).c_str(), L"rb", _SH_DENYWR);
#else
  file = fopen(name, "rb");
#endif
  if (file) {
    char buf[4] = {0};
    fread(buf, 1, 4, file);
    if (memcmp(buf, "JOYE", 4) == 0) {
      fclose(file);
      return true;
    }
    fclose(file);
    return false;
  }
  return false;
}

class EncryptReader : public av::CustomIO {
public:
  EncryptReader(const std::string &name, const std::string &password)
      : _name(name), _password(password) {
    _reader = enc_reader_new();
    auto ret = enc_reader_open(_reader, _name.c_str(), password.c_str());
    if (ret != 1) {
      enc_reader_close(_reader);
      _reader = nullptr;
    }
  }
  ~EncryptReader() {
    if (_reader) {
      enc_reader_close(_reader);
    }
  }

  int read(uint8_t *data, size_t size) override {
    if (_reader) {
      return (int)enc_reader_read(_reader, data, size);
    }
    return AVERROR(EIO);
  }

  int64_t seek(int64_t offset, int whence) override {
    if (!_reader) {
      return AVERROR(EIO);
    }
    if (whence == AVSEEK_SIZE) {
      return enc_reader_file_size(_reader);
    }

    int rc = enc_reader_seek(_reader, offset, whence);
    if (rc == 1) {
      return enc_reader_tell(_reader);
    }
    return AVERROR(EIO);
  }

  int write(const uint8_t *data, size_t size) override {
    assert(false);
    return -1;
  }

  int seekable() const override { return 1; }
  const char *name() const override { return _name.c_str(); }

private:
  std::string _name;
  std::string _password;
  enc_reader_ctx *_reader = nullptr;
};

class FILEReader : public av::CustomIO {
public:
  FILEReader(const std::string &name) : _name(name) {
#ifdef _WIN32
    _file = _wfsopen(Utf8ToWide(_name).c_str(), L"rb", _SH_DENYWR);
#else
    _file = fopen(_name.c_str(), "rb");
#endif
  }
  ~FILEReader() {
    if (_file) {
      fclose(_file);
    }
  }

  int read(uint8_t *data, size_t size) override {
    if (!_file) {
      return AVERROR(EIO);
    }
    return fread(data, 1, size, _file);
  }

  int64_t seek(int64_t offset, int whence) override {
    if (!_file) {
      return AVERROR(EIO);
    }

    if (whence == AVSEEK_SIZE) {
      int64_t pos = _fseeki64(_file, 0, SEEK_CUR);
      int64_t size = _fseeki64(_file, 0, SEEK_END);
      _fseeki64(_file, pos, SEEK_SET);
      return size;
    }

    int rc = fseek(_file, offset, whence);
    if (rc == 0) {
      return ftell(_file);
    }
    return AVERROR(EIO);
  }

  int write(const uint8_t *data, size_t size) override {
    assert(false);
    return -1;
  }

  int seekable() const override { return 1; }
  const char *name() const override { return _name.c_str(); }

private:
  std::string _name;
  FILE *_file = nullptr;
};

static av::CustomIO * customio_open_read(const char* filename, const char* password) {
  if (is_enc_file(filename)) {
    return new EncryptReader(filename, password);
  }
  return new FILEReader(filename);
}

static int customio_read(void *opaque, uint8_t *buf, int buf_size) {
  av::CustomIO *io = static_cast<av::CustomIO*>(opaque);
  int bytes = io->read(buf, buf_size);
  if (bytes == 0) {
    return AVERROR_EOF;
  }
  return bytes;
}

static int64_t customio_seek(void *opaque, int64_t offset, int whence) {
  av::CustomIO *io = static_cast<av::CustomIO*>(opaque);
  return io->seek(offset, whence);
}

#endif
