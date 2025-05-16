#include "webm_muxer.h"
#include "fixwebm.h"

int FixWebmFile(const char* input, const char* output) {
    auto ok = remuxWebmFile(input, output, {});
    return ok ? 0 : 1;
}
