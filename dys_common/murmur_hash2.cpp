#include <stdint.h>
#include "murmur_hash2.h"

namespace base {
  uint32_t MurmurHash2(const void * key, uint32_t len, uint32_t seed) {
    // 'm' and 'r' are mixing constants generated offline.
    // They're not really 'magic', they just happen to work well.
    const unsigned int m = 0x5bd1e995;
    const int r = 24;

    // Initialize the hash to a 'random' value
    unsigned int h = seed ^ len;
    // Mix 4 bytes at a time into the hash
    const unsigned char * data = (const unsigned char *)key;

    while(len >= 4) {
      unsigned int k = *(unsigned int *)data;
      k *= m;
      k ^= k >> r;
      k *= m;
      h *= m;
      h ^= k;

      data += 4;
      len -= 4;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    // Handle the last few bytes of the input array
    switch(len) {
    case 3: h ^= data[2] << 16;
    case 2: h ^= data[1] << 8;
    case 1: h ^= data[0];
      h *= m;
    };
#pragma GCC diagnostic pop

    // Do a few final mixes of the hash to ensure the last few
    // bytes are well-incorporated.
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    return h;
  }
} // namespace base
