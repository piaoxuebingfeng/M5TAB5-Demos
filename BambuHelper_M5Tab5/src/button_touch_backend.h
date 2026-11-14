#ifndef BUTTON_TOUCH_BACKEND_H
#define BUTTON_TOUCH_BACKEND_H

#include <Arduino.h>

// Touch-driver backend interface. Exactly one .cpp compiles a body, selected by
// whole-file guards (mirrors buzzer_backend_*.cpp): button_touch_<driver>.cpp for
// each explicit USE_* driver, and button_touch_none.cpp when none is selected
// (which itself provides the LovyanGFX TOUCH_CS fallback or a true no-op). All
// files are compiled; the guards make only one define the symbols.
//
// At most one EXPLICIT driver may be selected. Zero is valid (no-touch boards -
// esp32s3, esp32c3, the round/DIY envs). More than one is a build error.
#if (defined(USE_XPT2046) + defined(USE_CST816) + defined(USE_CST328) \
     + defined(USE_FT5X06) + defined(USE_FT6336) + defined(USE_AXS_TOUCH) \
     + defined(USE_GT911)) > 1
#error "button_touch: more than one explicit touch driver (USE_*) selected"
#endif

// Default AXS touch INT GPIO. Defined here (not just in the backend .cpp) because
// sanitizeButtonPin() in button.cpp also needs it to reject a conflicting pin.
#if defined(USE_AXS_TOUCH) && !defined(AXS_TOUCH_INT)
#define AXS_TOUCH_INT 3
#endif

enum class TouchEvent : uint8_t {
  None,         // no edge this tick; `isDown` carries the current level
  Pressed,      // edge-managed backends only: press edge
  Released,     // edge-managed backends only: release edge
  Unavailable,  // bus/read failure - caller must leave debounce/hold state untouched
};

struct TouchPoll {
  TouchEvent ev;
  bool isDown;  // meaningful for level backends (ev == None); raw finger-down
};

// Bring up the touch bus/pins + first probe (all logging inside). Called from
// initButton() only when the configured button type is a touchscreen.
void touchInit();

// Poll the touch controller once. Level backends return {None, level} or
// {Unavailable,false}; the edge-managed backend returns {Pressed|Released|None,...}.
TouchPoll touchPoll();

#if defined(USE_GT911)
struct TouchDiagnostics {
  bool ready;
  bool down;
  uint32_t frames;
  uint32_t presses;
  uint32_t readErrors;
};
TouchDiagnostics touchDiagnostics();

// Raw GT911 state for bring-up: everything needed to tell "asleep" from
// "mis-configured" from "never polled" without a logic analyser.
struct TouchRegisterDump {
  uint32_t polls;        // touchPoll() invocations since boot
  uint8_t  addr;         // address actually in use (0x14 or 0x5D)
  uint8_t  status;       // 0x814E read with STOP in between
  uint8_t  statusRestart;// 0x814E read with a repeated START
  uint8_t  cmdReg;       // 0x8040: 0x00 normal, 0x05 sleep
  uint8_t  configVersion;// 0x8047
  uint8_t  configVerRS;  // 0x8047 read with a repeated START
  uint8_t  refreshRate;  // 0x8056 low nibble + 5 (ms)
  uint8_t  fwHigh;       // 0x8144
  uint8_t  fwLow;        // 0x8145
  uint8_t  checksum;     // 0x80FF
  int      intLevel;     // sampled GT911_INT pin
  char     product[5];   // 0x8140..0x8143
};
TouchRegisterDump touchRegisterDump();
// Issue the documented wake pulse (INT low ~58 ms). Idempotent.
void touchWake();
#if defined(BOARD_IS_WS_P4_7B)
struct TouchContact { bool down; bool valid; uint16_t x; uint16_t y; };
// Snapshot of the same poll consumed by button.cpp; never polls I2C twice.
TouchContact touchContact();
#endif
#endif

#endif  // BUTTON_TOUCH_BACKEND_H
