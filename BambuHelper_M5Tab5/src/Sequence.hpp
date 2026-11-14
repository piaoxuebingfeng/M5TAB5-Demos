#pragma once

#include <cstdint>

namespace obn {
namespace camera {
namespace tutk {

// Packet and frame numbers wrap independently. Return a signed serial-number
// distance so a candidate just after `newest` is negative and one just before
// it is positive, including across the 16-bit wrap boundary.
inline int32_t sequence_age(uint16_t newest, uint16_t candidate) {
  const uint16_t distance = static_cast<uint16_t>(newest - candidate);
  return distance < 0x8000u ? static_cast<int32_t>(distance)
                            : static_cast<int32_t>(distance) - 0x10000;
}

inline bool sequence_before(uint32_t left, uint32_t right) {
  return int32_t(left - right) < 0;
}

}
}
}
