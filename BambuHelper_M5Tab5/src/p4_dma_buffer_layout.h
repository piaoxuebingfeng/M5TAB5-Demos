#pragma once
#include <cstddef>
#include <limits>

// DMA into cached PSRAM needs both the address and the allocation rounded to
// the selected cache line. Reject overflow before calling the heap API.
struct P4DmaBufferLayout {
  size_t alignment = 0;
  size_t bytes = 0;
};

inline bool p4DmaBufferLayout(size_t bytes, size_t requestedAlignment,
                              size_t cacheAlignment, P4DmaBufferLayout& out) {
  out = {};
  const size_t alignment = requestedAlignment > cacheAlignment
    ? requestedAlignment : cacheAlignment;
  if (!bytes || !alignment || (alignment & (alignment - 1)) ||
      bytes > std::numeric_limits<size_t>::max() - (alignment - 1)) return false;
  out.alignment = alignment;
  out.bytes = (bytes + alignment - 1) & ~(alignment - 1);
  return true;
}
