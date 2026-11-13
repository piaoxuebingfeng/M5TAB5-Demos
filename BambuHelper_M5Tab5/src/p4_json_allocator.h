#pragma once

#include <ArduinoJson.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#if defined(BOARD_IS_WS_P4_7B) && __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define P4_JSON_EXTERNAL_MEMORY 1
#endif

// Track allocation provenance so resizing preserves its original heap region
// and every block uses the matching heap_caps_free()/free() release path.
class P4JsonAllocator final : public ArduinoJson::Allocator {
  static constexpr uint64_t marker = 0x50344a534f4e414cULL;
  struct alignas(16) Header { uint64_t tag; size_t bytes; uint32_t caps; };

  static void* allocateBytes(size_t bytes, uint32_t preferredCaps = 0) {
    if (bytes > std::numeric_limits<size_t>::max() - sizeof(Header)) return nullptr;
#if defined(P4_JSON_EXTERNAL_MEMORY)
    // A full external heap is a parse failure, not permission to consume the
    // final internal reserve needed by Wi-Fi and TLS.
    const uint32_t caps = preferredCaps ? preferredCaps :
      bytes >= 1024 ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                    : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void* raw = heap_caps_malloc(sizeof(Header) + bytes, caps);
#else
    (void)preferredCaps;
    void* raw = std::malloc(sizeof(Header) + bytes);
#endif
    if (!raw) return nullptr;
    auto* header = static_cast<Header*>(raw);
    header->tag = marker;
    header->bytes = bytes;
    header->caps = 0;
#if defined(P4_JSON_EXTERNAL_MEMORY)
    header->caps = caps;
#endif
    return header + 1;
  }

  static Header* header(void* ptr) {
    return ptr ? static_cast<Header*>(ptr) - 1 : nullptr;
  }

 public:
  void* allocate(size_t size) override { return size ? allocateBytes(size) : nullptr; }

  void deallocate(void* ptr) override {
    Header* original = header(ptr);
    if (!original || original->tag != marker) return;
    original->tag = 0;
#if defined(P4_JSON_EXTERNAL_MEMORY)
    heap_caps_free(original);
#else
    std::free(original);
#endif
  }

  void* reallocate(void* ptr, size_t newSize) override {
    if (!ptr) return allocate(newSize);
    if (!newSize) { deallocate(ptr); return nullptr; }
    Header* original = header(ptr);
    if (original->tag != marker) return nullptr;
    // ArduinoJson 7.4.3's StringNode::resize() assumes shrinking cannot
    // fail, then dereferences the result. Retaining the original block is a
    // valid shrink policy and keeps that assumption true even when PSRAM is
    // exhausted. StringBuilder's destructor still releases the original.
    if (newSize <= original->bytes) return ptr;
    // Preserve the original region when resizing a small external block.
    // Reclassification by the new size could move it into internal RAM.
#if defined(P4_JSON_EXTERNAL_MEMORY)
    const uint32_t caps = original->caps;
#else
    const uint32_t caps = 0;
#endif
    void* replacement = allocateBytes(newSize, caps);
    if (!replacement) return nullptr;  // The old block remains owned by JSON.
    std::memcpy(replacement, ptr, std::min(original->bytes, newSize));
    deallocate(ptr);
    return replacement;
  }
};

inline ArduinoJson::Allocator* p4JsonAllocator() {
#if defined(P4_JSON_EXTERNAL_MEMORY)
  static P4JsonAllocator allocator;
  return &allocator;
#else
  return ArduinoJson::detail::DefaultAllocator::instance();
#endif
}
