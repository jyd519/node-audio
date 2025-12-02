#include "webm_muxer.h"
#include "fixwebm.h"

int FixWebmFile(const char* input, const char* output) {
    auto ok = remuxWebmFile(input, output, {});
    return ok ? 0 : 1;
}

int FixWebmFile2(const char* input, const char* output,
  const char* metadata) {
  std::map<std::string, std::string> tags;
  if (metadata) {
    std::string key, value;
    // key1=value1;key2=value2...
    const char* p = metadata;
    while (*p) {
      int n = strcspn(p, "=");
      if (n == 0)
        break;

      key = std::string(p, n);
      p += n + 1;

      if (*p == '"') {
        const char *next = p + 1;
        for (; *next;) {
          if (*next == '\\') { // escape char
            next += 2;
          } else {
            if (*next == '"') {
              value = std::string(p + 1, next - p - 1);
              n = strcspn(next, ";");
              p = next + n + 1;
              break;
            }
            next += 1;
          }
        }
      } else {
        n = strcspn(p, ";");
        if (n == 0)
          break;

        value = std::string(p, n);
        value[n] = 0;
        p = p + n + 1;
      }

      tags[key] = value;
    }
  }
  auto ok = remuxWebmFile(input, output, tags);
  return ok ? 0 : 1;
}
