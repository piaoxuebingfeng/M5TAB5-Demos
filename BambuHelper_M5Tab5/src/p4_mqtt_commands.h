#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// Configuration, recovery and a manual refresh have different lifetimes.
// Never clear all three while handling only one of them. Stop/Resume ordering
// remains in P4MqttLifecycle.
template<size_t N>
class P4MqttCommands {
  std::atomic<bool> configure_[N]{};
  std::atomic<bool> reconnect_[N]{};
  std::atomic<bool> refresh_[N]{};
public:
  void configure(size_t slot) { if (slot < N) configure_[slot].store(true, std::memory_order_release); }
  void reconnect(size_t slot) { if (slot < N) reconnect_[slot].store(true, std::memory_order_release); }
  void refresh(size_t slot) { if (slot < N) refresh_[slot].store(true, std::memory_order_release); }
  bool takeConfigure(size_t slot) { return slot < N && configure_[slot].exchange(false, std::memory_order_acq_rel); }
  bool takeReconnect(size_t slot) { return slot < N && reconnect_[slot].exchange(false, std::memory_order_acq_rel); }
  bool takeRefresh(size_t slot) { return slot < N && refresh_[slot].exchange(false, std::memory_order_acq_rel); }
  void discard(size_t slot) {
    if (slot >= N) return;
    configure_[slot].store(false, std::memory_order_release);
    reconnect_[slot].store(false, std::memory_order_release);
    refresh_[slot].store(false, std::memory_order_release);
  }
  void discardTransient(size_t slot) {
    if (slot >= N) return;
    reconnect_[slot].store(false, std::memory_order_release);
    refresh_[slot].store(false, std::memory_order_release);
  }
};
