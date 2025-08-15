#pragma once
#include <iostream>

namespace base {

  uint32_t MurmurHash2(const void * key, uint32_t len, uint32_t seed=123);

} // namespace base

