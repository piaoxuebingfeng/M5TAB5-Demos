#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#if defined(P4_MQTT_LIFECYCLE_TEST_HOOK)
#include <functional>
#endif

// A single cross-producer sequence orders stop and resume even when the worker
// drains their flags in a different order. Configuration is a separate latest
// value; Resume reads that value after the most recent lifecycle transition.
template<size_t N>
class P4MqttLifecycle {
  std::atomic<uint32_t> sequence_{2};
  std::atomic<uint32_t> resume_{1};
  std::atomic<uint32_t> stop_[N]{};
  static void publishNewest(std::atomic<uint32_t>& target, uint32_t value) {
    uint32_t current = target.load(std::memory_order_relaxed);
    while (current < value && !target.compare_exchange_weak(
        current, value, std::memory_order_release, std::memory_order_relaxed)) {}
  }
#if defined(P4_MQTT_LIFECYCLE_TEST_HOOK)
  std::function<void(uint32_t)> afterSequence_;
#endif
public:
  void stop(size_t slot) {
    if (slot >= N) return;
    const uint32_t sequence = sequence_.fetch_add(1, std::memory_order_acq_rel);
#if defined(P4_MQTT_LIFECYCLE_TEST_HOOK)
    if (afterSequence_) afterSequence_(sequence);
#endif
    publishNewest(stop_[slot], sequence);
  }
  void resume() {
    const uint32_t sequence = sequence_.fetch_add(1, std::memory_order_acq_rel);
#if defined(P4_MQTT_LIFECYCLE_TEST_HOOK)
    if (afterSequence_) afterSequence_(sequence);
#endif
    publishNewest(resume_, sequence);
  }
  bool stopped(size_t slot) const {
    return slot < N && stop_[slot].load(std::memory_order_acquire) >= resume_.load(std::memory_order_acquire);
  }
#if defined(P4_MQTT_LIFECYCLE_TEST_HOOK)
  void testHook(std::function<void(uint32_t)> hook) { afterSequence_ = std::move(hook); }
#endif
};
