#include "webm_muxer.h"
#include "fixwebm.h"

int FixWebmFile(const char* input, const char* output) {
    return remuxWebmFile(input, output, {});
}
