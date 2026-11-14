// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

#include "p4_tls_memory.h"
#if defined(BOARD_IS_WS_P4_7B)
#include <Arduino.h>
#include <atomic>
#include <limits>
#include <esp_heap_caps.h>
#include <esp_private/esp_cache_private.h>
#include <mbedtls/platform.h>
#include "p4_dma_buffer_layout.h"

namespace {
size_t cacheAlignment = 0;
std::atomic<uint32_t> externalAllocations{0}, externalFailures{0};
std::atomic<uint32_t> failedAllocations{0}, lastFailureBytes{0}, lastFailureCaps{0};

// The heap callback can run inside a low-level allocation. Record only; all
// logging stays in the low-rate health task after the allocation has returned.
void IRAM_ATTR allocationFailed(size_t bytes, uint32_t caps, const char*) {
  lastFailureBytes.store(bytes, std::memory_order_relaxed);
  lastFailureCaps.store(caps, std::memory_order_relaxed);
  failedAllocations.fetch_add(1, std::memory_order_relaxed);
}

void* allocate(size_t count, size_t elementSize) {
  if (!count || !elementSize || elementSize > std::numeric_limits<size_t>::max() / count)
    return nullptr;
  const size_t bytes = count * elementSize;
  // SSL record buffers and large certificate workspaces must not consume the
  // DMA-capable internal region needed by AES descriptors and the SDIO port.
  // Small crypto contexts retain the SDK's fast internal-memory policy.
  if (bytes >= 2048) {
    P4DmaBufferLayout layout;
    if (!p4DmaBufferLayout(bytes, cacheAlignment, cacheAlignment, layout)) return nullptr;
    void* memory = heap_caps_aligned_calloc(layout.alignment, 1, layout.bytes,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (memory) {
      externalAllocations.fetch_add(1, std::memory_order_relaxed);
      return memory;
    }
    externalFailures.fetch_add(1, std::memory_order_relaxed);
  }
  return heap_caps_calloc(count, elementSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
}

void p4TlsMemoryStart() {
  if (cacheAlignment) return;
  if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cacheAlignment) != ESP_OK ||
      !cacheAlignment || mbedtls_platform_set_calloc_free(allocate, heap_caps_free) != 0) {
    cacheAlignment = 0;
    Serial.println("[tls-memory] allocator initialization failed");
    return;
  }
  Serial.printf("[tls-memory] large buffers use PSRAM alignment=%u\n", (unsigned)cacheAlignment);
  esp_err_t result = heap_caps_register_failed_alloc_callback(allocationFailed);
  if (result != ESP_OK) Serial.printf("[tls-memory] heap diagnostic hook unavailable rc=%d\n", int(result));
}

void p4TlsMemoryLog() {
  Serial.printf("[tls-memory] psram_allocations=%lu failures=%lu heap_failures=%lu last_bytes=%lu last_caps=0x%lx\n",
                (unsigned long)externalAllocations.load(std::memory_order_relaxed),
                (unsigned long)externalFailures.load(std::memory_order_relaxed),
                (unsigned long)failedAllocations.load(std::memory_order_relaxed),
                (unsigned long)lastFailureBytes.load(std::memory_order_relaxed),
                (unsigned long)lastFailureCaps.load(std::memory_order_relaxed));
}
#endif
