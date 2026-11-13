#pragma once

#include <atomic>
#include <cstdint>

// MQTT keeps its own independent socket. This gate only limits optional cloud
// HTTPS and camera transfers to one auxiliary connection at a time. Every
// caller uses tryAcquire: neither the renderer nor another service waits for a
// slow peer. Busy is a scheduling outcome, not a network failure.
enum class P4AuxTransportOwner : uint8_t { None = 0, Cloud = 1, Camera = 2 };

class P4AuxTransportGate {
  std::atomic<P4AuxTransportOwner> owner_{P4AuxTransportOwner::None};
public:
  bool tryAcquire(P4AuxTransportOwner owner) {
    P4AuxTransportOwner expected = P4AuxTransportOwner::None;
    return owner != P4AuxTransportOwner::None &&
      owner_.compare_exchange_strong(expected, owner, std::memory_order_acq_rel,
                                     std::memory_order_relaxed);
  }
  void release(P4AuxTransportOwner owner) {
    P4AuxTransportOwner expected = owner;
    owner_.compare_exchange_strong(expected, P4AuxTransportOwner::None,
                                   std::memory_order_acq_rel, std::memory_order_relaxed);
  }
  P4AuxTransportOwner owner() const { return owner_.load(std::memory_order_acquire); }
};
