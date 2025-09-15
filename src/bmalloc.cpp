/**
 * Copyright The bmalloc Contributors
 */

#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif
__attribute__((weak)) void *memset(void *dest, int val, size_t n) {
  unsigned char *ptr = (unsigned char *)dest;
  while (n-- > 0) {
    *ptr++ = val;
  }
  return dest;
}
#ifdef __cplusplus
}
#endif
