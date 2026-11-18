#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline bool fakeHeapCapsFail = false;
inline size_t fakeHeapCapsRequestedBytes = 0;
inline void* heap_caps_malloc(size_t size, int) {
  fakeHeapCapsRequestedBytes += size;
  return fakeHeapCapsFail ? nullptr : malloc(size);
}
