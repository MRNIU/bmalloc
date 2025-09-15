/**
 * Copyright The bmalloc Contributors
 */

#include <cstring>

extern "C" {
__attribute__((weak)) void* memset(void* ptr, int value, size_t size) {
  // Default implementation - could use inline assembly, SIMD, or other
  // optimizations
  if (!ptr || size == 0) {
    return ptr;
  }

  unsigned char* byte_ptr = static_cast<unsigned char*>(ptr);
  unsigned char byte_value = static_cast<unsigned char>(value);

  for (size_t i = 0; i < size; ++i) {
    byte_ptr[i] = byte_value;
  }

  return ptr;
}
}
