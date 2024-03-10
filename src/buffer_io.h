#pragma once

#include <stdint.h>

struct buffer_data {
  uint8_t *ptr;
  size_t size; ///< size left in the buffer
};

#ifdef __cplusplus
extern "C" {
#endif

extern int read_packet(void *opaque, uint8_t *buf, int buf_size);

#ifdef __cplusplus
}
#endif
