#pragma once

#include <cstring>

// A worker may publish whenever its state or diagnostics actually changes.
// The cache lives in that single producer; the returned count is testable
// without an MQTT socket or an Arduino heap API.
template<typename State, typename Diagnostics>
class P4SnapshotDiff {
  State state_{};
  Diagnostics diagnostics_{};
  bool valid_ = false;
public:
  bool changed(const State& state, const Diagnostics& diagnostics) {
    if (valid_ && std::memcmp(&state_, &state, sizeof(State)) == 0 &&
        std::memcmp(&diagnostics_, &diagnostics, sizeof(Diagnostics)) == 0) return false;
    state_ = state;
    diagnostics_ = diagnostics;
    valid_ = true;
    return true;
  }
  // A failed publication must not make an unchanged final snapshot disappear.
  void invalidate() { valid_ = false; }
};
