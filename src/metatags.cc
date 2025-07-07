#include "metatags.h"

#include "avcpp/formatcontext.h"

#include "enc_reader.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/log.h>
#ifdef __cplusplus
}
#endif

std::map<std::string, std::string> readMediaFileFormatTags(const std::string &filePath, const std::string& password) {
  std::map<std::string, std::string> tags;
  try {
    std::unique_ptr<EncryptReader> io;
    av::FormatContext formatContext;

    if (!password.empty() && is_enc_file(filePath.c_str())) {
      io.reset(new EncryptReader(filePath, password));
      formatContext.openInput(io.get());
    } else {
      formatContext.openInput(filePath);
    }

    formatContext.findStreamInfo();

    AVFormatContext *avFormatContext = formatContext.raw();
    // 遍历 metadata (AVDictionary)
    AVDictionaryEntry *tag = nullptr;
    while ((tag = av_dict_get(avFormatContext->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
      tags[tag->key] = tag->value;
    }

    formatContext.close();
  } catch (const std::exception &e) {
    av_log(nullptr, AV_LOG_ERROR, "Unexpected error in readMediaFileFormatTags: %s\n", e.what());
  }

  return tags;
}
