#pragma once

#include <atomic>
#include <cstdint>

// Only the web task arms a save. The storage owner completes it later. Ticket
// transitions are CAS operations so concurrent failure and completion cannot
// overwrite each other's result or affect a newer request.
class P4RestartGate {
  std::atomic<uint32_t> ticket_{0};
  uint32_t nextTicket_ = 0;

public:
  uint32_t arm() {
    if (++nextTicket_ == 0) ++nextTicket_;
    ticket_.store(nextTicket_, std::memory_order_release);
    return nextTicket_;
  }
  void cancel(uint32_t ticket) {
    uint32_t expected = ticket;
    if (ticket) ticket_.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
  }
  bool complete(uint32_t ticket, bool stored) {
    // Success keeps the ticket armed for the web task's delayed reboot. A
    // failure consumes and clears it; callers must always inspect `stored`.
    const uint32_t result = stored ? ticket : 0;
    uint32_t expected = ticket;
    return ticket && ticket_.compare_exchange_strong(expected, result,
                                                     std::memory_order_acq_rel);
  }
  bool scheduled(uint32_t ticket) const {
    return ticket && ticket_.load(std::memory_order_acquire) == ticket;
  }
  bool take(uint32_t ticket) {
    uint32_t expected = ticket;
    return ticket && ticket_.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
  }
};
