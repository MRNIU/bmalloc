/**
 * Copyright The bmalloc Contributors
 */

#include <cstring>

extern "C" {
/**
 * @brief Weak implementation of memset function that can be overridden
 * @param ptr Pointer to the memory area to be filled
 * @param value Value to be set (will be converted to unsigned char)
 * @param size Number of bytes to be set
 * @return void* Returns the pointer to the memory area ptr
 * @note This function uses weak linkage, allowing users to provide their own
 *       implementation that will override this default one at link time
 * @example To override this function, define your own memset:
 *          extern "C" void* memset(void* ptr, int value, size_t size) {
 *              // Your custom implementation here
 *              return your_optimized_memset(ptr, value, size);
 *          }
 */
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
