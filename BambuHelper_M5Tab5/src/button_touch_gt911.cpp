// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

// GT911 capacitive touch backend (ws_lcd_28c / ws_p4_lcd_7b). See button_touch_backend.h.
// Differences vs CST328:
//   - I2C address is latched at reset from the INT level: low = 0x5D, high = 0x14
//   - status register 0x814E: bit7 = data ready, low nibble = point count
//   - the status byte MUST be written back as 0 after every read, or the
//     controller never raises another frame
//
// The reset + INT-low latch does NOT happen here. It runs in initDisplay()
// (src/display_ui.cpp), because the touch reset line is a TCA9554 expander bit
// that the display bring-up already owns, and touchInit() only runs from
// handleSplashPhase() about two seconds later - far too late to influence the
// address. Do not "clean that up" into this file.
#include "button_touch_backend.h"

#if defined(USE_GT911)

#include <Wire.h>

// Primary address (INT held low through reset). 0x14 is the INT-high variant;
// probed as a fallback so a board that latched the other way still works.
#define GT911_ADDR_LOW    0x5D
#define GT911_ADDR_HIGH   0x14
#define GT911_REG_STATUS  0x814E
#define GT911_REG_PRODUCT 0x8140
#define GT911_MAX_POINTS  5

static uint8_t addr     = GT911_ADDR_LOW;
static bool    busReady = false;
static bool    seen     = false;
// Last published finger level. 0x814E bit7 is a data-ready EDGE, not a level:
// between report frames it reads 0, while button.cpp's shared debouncer expects
// a level. Reporting "not down" on those polls chops a held finger into a burst
// of taps and breaks every hold gesture (LED dim, power-confirm ring,
// hold-to-power-on), so the last frame's level stands until the next one.
static bool     lastDown    = false;
static uint32_t lastFrameMs = 0;
static uint32_t frameCount = 0, pressCount = 0, readErrors = 0;
#if defined(BOARD_IS_WS_P4_7B)
static uint16_t contactX = 0, contactY = 0;
static bool contactValid = false;
TouchContact touchContact() { return {lastDown, contactValid, contactX, contactY}; }
#endif
// A controller that stops publishing while we believe a finger is down would
// otherwise latch a permanent hold - which the power-confirm ring reads as a
// deliberate hold and would act on. Nothing legitimately withholds a frame for
// a second while touched.
static const uint32_t DOWN_STALE_MS = 1000;

// GT911 register addresses are 16-bit, MSB first (CST816/CST328 use 8-bit).
static bool gt911Read(uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static bool gt911Write(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)reg);
  Wire.write(value);
  return Wire.endTransmission(true) == 0;
}

static bool gt911Probe(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission(true) == 0;
}

void touchInit() {
  // ws_lcd_28c latches the address during display reset. LCD-7B leaves
  // INT/RST unassigned and discovers the controller by polling.
  #if GT911_INT >= 0
  pinMode(GT911_INT, INPUT);
  #endif
  lastDown    = false;
  lastFrameMs = 0;
#if defined(BOARD_IS_WS_P4_7B)
  contactValid = false;
#endif

  Wire.begin(GT911_SDA, GT911_SCL);
  Wire.setClock(400000);
  busReady = true;

  // One-shot bus scan: the address depends on a reset-time pin level, so a
  // wrong answer here is the first thing to look at on new hardware.
  Serial.print("GT911: I2C scan ->");
  uint8_t found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (gt911Probe(a)) { Serial.printf(" 0x%02X", a); found++; }
  }
  if (!found) Serial.print(" (nothing)");
  Serial.println();

  if (!gt911Probe(addr) && gt911Probe(GT911_ADDR_HIGH)) {
    addr = GT911_ADDR_HIGH;
    Serial.println("GT911: reset latched the INT-high address");
  }

  uint8_t id[4] = {0};
  if (gt911Probe(addr) && gt911Read(GT911_REG_PRODUCT, id, 4)) {
    Serial.printf("GT911 touch initialized (addr 0x%02X, SDA=%d SCL=%d INT=%d, id %c%c%c%c)\n",
                  addr, GT911_SDA, GT911_SCL, GT911_INT,
                  id[0] ? id[0] : '?', id[1] ? id[1] : '?',
                  id[2] ? id[2] : '?', id[3] ? id[3] : '?');
    seen = true;
    gt911Write(GT911_REG_STATUS, 0);   // start from a clean status byte
  } else {
    Serial.printf("GT911 touch did not answer at init (addr 0x%02X, SDA=%d SCL=%d); will keep retrying at runtime\n",
                  addr, GT911_SDA, GT911_SCL);
  }
}

TouchPoll touchPoll() {
  if (!busReady) return {TouchEvent::Unavailable, false};

  uint8_t status = 0;
  if (!gt911Read(GT911_REG_STATUS, &status, 1)) {
    readErrors++;
#if defined(BOARD_IS_WS_P4_7B)
    contactValid = false;
#endif
    return {TouchEvent::Unavailable, false};
  }
  if (!seen) {
    Serial.printf("GT911 touch became responsive at runtime (addr 0x%02X)\n", addr);
    seen = true;
  }

  // bit7 clear = the controller has not published a new frame, which is the
  // normal case between report periods. Repeat the last known level and skip
  // the acknowledge write - there is no frame to acknowledge, and the loop
  // polls far faster than the GT911 reports.
  if ((status & 0x80) == 0) {
    if (lastDown && (millis() - lastFrameMs) > DOWN_STALE_MS) {
      lastDown = false;
#if defined(BOARD_IS_WS_P4_7B)
      contactValid = false;
#endif
    }
    return {TouchEvent::None, lastDown};
  }

  const uint8_t points = status & 0x0F;
#if defined(BOARD_IS_WS_P4_7B)
  // Read coordinates BEFORE acknowledging the frame. Multiple fingers, a
  // failed read or out-of-bounds coordinates cancel navigation until release.
  if (points == 1) {
    uint8_t xy[4];
    if (gt911Read(0x8150, xy, sizeof(xy))) {
      contactX = xy[0] | (xy[1] << 8);
      contactY = xy[2] | (xy[3] << 8);
#if defined(BOARD_IS_M5TAB5)
      // Bounds check against the controller's NATIVE frame (720x1280), not the
      // 1024x600 canvas: on the Tab5 the raw pair is still portrait here and the
      // landscape mapping happens later in p4ui::touchPoint(). Using the canvas
      // size would discard every tap below y=600.
      contactValid = contactX < M5TAB5_PANEL_RAW_W && contactY < M5TAB5_PANEL_RAW_H;
#else
      contactValid = contactX < 1024 && contactY < 600;
#endif
    } else { contactValid = false; readErrors++; }
  } else if (points != 0) contactValid = false;
#endif
  // Always acknowledge a published frame, whatever the count - skipping this
  // wedges the controller on its current frame and touch dies after one tap.
  if (!gt911Write(GT911_REG_STATUS, 0)) {
    readErrors++;
#if defined(BOARD_IS_WS_P4_7B)
    contactValid = false;
#endif
  }
  const bool down = (points > 0 && points <= GT911_MAX_POINTS);
  frameCount++;
  if (down && !lastDown) {
    pressCount++;
#if defined(M5TAB5_TOUCH_TRACE)
    // Bring-up aid: the raw pair is in the panel's native frame, so this line
    // is what tells you whether the 720x1280 -> 1280x720 mapping is sane.
    Serial.printf("[touch] press raw=(%u,%u) points=%u valid=%d\n",
                  contactX, contactY, points, (int)contactValid);
#endif
  }
  lastDown    = down;
  lastFrameMs = millis();
  return {TouchEvent::None, lastDown};
}

TouchDiagnostics touchDiagnostics() {
  return {busReady && seen, lastDown, frameCount, pressCount, readErrors};
}

#endif  // USE_GT911
