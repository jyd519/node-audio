#ifdef _WIN32
#include <stdlib.h> // For _byteswap_uint64
#include <intrin.h>  // For _byteswap_uint64 on some compilers
#include <stdint.h>
#if defined(__GNUC__)
#include <byteswap.h>
#define bswap_64(x) __bswap_constant_64(x)
#elif defined(_MSC_VER)
#include <stdlib.h>
#define bswap_64(x) _byteswap_uint64(x)
#else
static inline uint64_t bswap_64(uint64_t x) {
  return (((x & 0xff00000000000000ull) >> 56) |
          ((x & 0x00ff000000000000ull) >> 40) |
          ((x & 0x0000ff0000000000ull) >> 24) |
          ((x & 0x000000ff00000000ull) >> 8) |
          ((x & 0x00000000ff000000ull) << 8) |
          ((x & 0x0000000000ff0000ull) << 24) |
          ((x & 0x000000000000ff00ull) << 40) |
          ((x & 0x00000000000000ffull) << 56));
}
#endif

static uint64_t htobe64(uint64_t host_value) {
  // Check if the system is little-endian.  This is a compile-time check.
  volatile uint32_t i = 0x01234567;
  // LSB first implies little endian
  if (((uint8_t*)&i)[0] == 0x67) {
    return bswap_64(host_value);
  } else {
    return host_value;
  }
}
#endif

