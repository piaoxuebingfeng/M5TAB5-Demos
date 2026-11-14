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

#if defined(BOARD_IS_M5TAB5)
#include "m5tab5_board_io.h"   // m5tab5TouchReset(): TP_RST lives on PI4IOE-1
#endif

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
// Every touchPoll() invocation. frames stays 0 both when the controller has
// nothing to report AND when nobody ever polls it, so the two need to be
// distinguishable from the outside - this counter is that discriminator.
static uint32_t pollCount = 0;
// Live status byte and the silent-controller watchdog state. Declared up here
// because touchPoll() updates them; the diagnostic helpers that consume them
// live at the bottom of the file.
static uint8_t lastStatusByte = 0;
static uint8_t wakeAttempts   = 0;
static void serviceSilentController();
static void dumpRegisters(const char* why);
static void dumpConfigBlock();
static bool repairBlankConfig();
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
  // A whole GT911 configuration block is 186 bytes; the core's default TX buffer
  // is 128, which would silently truncate a single-transaction download.
  Wire.setBufferSize(220);
  busReady = true;

#if defined(BOARD_IS_M5TAB5)
  // The display bring-up pulses TP_RST too, but roughly two seconds earlier.
  // A controller that came up in a non-normal mode then still answers I2C
  // (product id reads fine) while its whole config block reads back zero and it
  // never publishes a frame. Owning the reset here makes the power-up mode a
  // property of the touch driver instead of a side effect of display init.
  if (m5tab5IoReady()) {
    m5tab5TouchReset(true);   // INT high -> 0x14, the address M5GFX latches
    Serial.println("GT911: independent TP_RST pulse applied (INT high, 0x14)");
  }
#endif

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
    // Wake BEFORE judging the configuration. A GT911 that has been reset but not
    // woken still answers its product id, but reports an all-zero configuration
    // block and never sets 0x814E bit7 - which is exactly what an unconfigured
    // controller looks like. Without this the blank-config repair below fires on
    // a perfectly healthy chip, its download is (correctly) refused by a chip
    // whose configuration is merely hidden, and bring-up ends in a false
    // "configuration area is blank" verdict.
    touchWake();
    gt911Write(GT911_REG_STATUS, 0);
    dumpRegisters("post-init");
    dumpConfigBlock();
    if (repairBlankConfig()) { dumpRegisters("post-repair"); dumpConfigBlock(); }
  } else {
    Serial.printf("GT911 touch did not answer at init (addr 0x%02X, SDA=%d SCL=%d); will keep retrying at runtime\n",
                  addr, GT911_SDA, GT911_SCL);
  }
}

TouchPoll touchPoll() {
  if (!busReady) return {TouchEvent::Unavailable, false};
  pollCount++;

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
  lastStatusByte = status;
  // Bring-up watchdog: log (and try to wake) a controller that answers I2C but
  // has never published a single frame.
  serviceSilentController();

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
  // `frames` keeps its original meaning (data-ready frames published by the
  // controller); the poll count travels in TouchRegisterDump so the two cannot
  // be confused again.
  return {busReady && seen, lastDown, frameCount, pressCount, readErrors};
}

// --- Bring-up diagnostics -------------------------------------------------
// A controller that answers I2C (product id reads fine) but never sets 0x814E
// bit7 is either asleep, mis-configured or held in reset. The status byte alone
// cannot tell those apart, so the surrounding state is dumped as well.
//
// Every function below that touches the bus runs in the caller's task. Wire is
// NOT thread-safe, so the HTTP side only ever reads the cached snapshot - never
// this reader.
static bool gt911ReadRaw(uint16_t reg, uint8_t* buf, size_t len, bool repeatedStart) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)reg);
  if (Wire.endTransmission(!repeatedStart) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static TouchRegisterDump cachedDump{};
static bool dumpValid = false;

// I2C work - caller must be the task that also polls touch.
static void dumpRegisters(const char* why) {
  TouchRegisterDump d{};
  uint8_t v = 0;
  d.addr = addr;
  d.polls = pollCount;
  d.intLevel = (GT911_INT >= 0) ? digitalRead(GT911_INT) : -1;
  if (gt911Read(GT911_REG_STATUS, &v, 1)) d.status = v;
  if (gt911ReadRaw(GT911_REG_STATUS, &v, 1, true)) d.statusRestart = v;
  if (gt911Read(0x8040, &v, 1)) d.cmdReg = v;          // 0x00 normal, 0x05 sleep
  if (gt911Read(0x8047, &v, 1)) d.configVersion = v;
  if (gt911ReadRaw(0x8047, &v, 1, true)) d.configVerRS = v;
  if (gt911Read(0x8056, &v, 1)) d.refreshRate = v;
  uint8_t fw[2] = {0, 0};
  if (gt911Read(0x8144, fw, 2)) { d.fwHigh = fw[0]; d.fwLow = fw[1]; }
  uint8_t id[4] = {0, 0, 0, 0};
  if (gt911Read(GT911_REG_PRODUCT, id, 4)) {
    // Keep the JSON valid: any non-printable byte would break /debug parsing.
    for (int i = 0; i < 4; i++)
      d.product[i] = (id[i] >= 0x20 && id[i] < 0x7F) ? (char)id[i] : '.';
    d.product[4] = 0;
  }
  if (gt911Read(0x80FF, &v, 1)) d.checksum = v;
  cachedDump = d;
  dumpValid = true;
  Serial.printf("[gt911] %s: polls=%u addr=0x%02X status=0x%02X (rs=0x%02X) cmd=0x%02X cfg=%u/%u rr=%u fw=%u.%u cks=0x%02X int=%d id=%s\n",
                why, (unsigned)d.polls, d.addr, d.status, d.statusRestart, d.cmdReg,
                d.configVersion, d.configVerRS, d.refreshRate, d.fwHigh, d.fwLow,
                d.checksum, d.intLevel, d.product);
}

// One-off hex dump of the whole configuration area (0x8047..0x80FE). Chunked at
// 62 bytes because the Arduino Wire RX buffer is 128 bytes on ESP32-P4 - a single
// 186-byte requestFrom silently fails and looks like "all zeros".
static void dumpConfigBlock() {
  uint8_t block[186];
  bool ok = true;
  for (size_t off = 0; off < sizeof(block); off += 62) {
    const size_t len = (sizeof(block) - off < 62) ? (sizeof(block) - off) : 62;
    if (!gt911Read((uint16_t)(0x8047 + off), block + off, len)) { ok = false; break; }
  }
  if (!ok) { Serial.println("[gt911] config block read FAILED"); return; }
  uint16_t nonZero = 0;
  for (size_t i = 0; i < sizeof(block); i++) if (block[i]) nonZero++;
  Serial.printf("[gt911] config block 0x8047..0x80FE: %u/186 bytes non-zero\n", nonZero);
  for (size_t i = 0; i < sizeof(block); i += 32) {
    Serial.printf("  %04X:", 0x8047 + (unsigned)i);
    for (size_t j = i; j < i + 32 && j < sizeof(block); j++) Serial.printf(" %02X", block[j]);
    Serial.println();
  }
  // The two fields that decide whether the panel can report anything at all.
  Serial.printf("[gt911] X_Max=%u Y_Max=%u TouchNum=%u ModuleSwitch1=0x%02X 0x80FF=%02X\n",
                (unsigned)(block[0x8048 - 0x8047] | (block[0x8049 - 0x8047] << 8)),
                (unsigned)(block[0x804A - 0x8047] | (block[0x804B - 0x8047] << 8)),
                block[0x804C - 0x8047], block[0x804D - 0x8047], block[0x80FF - 0x8047]);
}

// --- Missing configuration ----------------------------------------------
// This unit's GT911 answers I2C and reports product id "911", but its whole
// configuration area (0x8047..0x80FE) reads back 0 - including X_Output_Max and
// Y_Output_Max. A GT911 with an empty configuration never starts scanning: no
// coordinate frames, 0x814E bit7 never sets, INT stays high, and from the
// outside the firmware looks like "controller ready but no touches, ever".
//
// The block below is the reference configuration published in the open-source
// arduino-goodix project (config version 'B'): 720 x 1280, 5 touch points - the
// exact geometry of the Tab5 panel. Bytes 0..183 are the configuration, 0x80FF
// holds its checksum and 0x8100 the "config fresh" commit flag, so one 186-byte
// write to 0x8047 applies the whole thing.
static const uint8_t GT911_REFERENCE_CONFIG[186] = {
  0x42, 0xD0, 0x02, 0x00, 0x05, 0x05, 0x75, 0x01, 0x01, 0x0F, 0x24, 0x0F, 0x64, 0x3C, 0x03, 0x05,
  0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x16, 0x19, 0x1C, 0x14, 0x8C, 0x0E, 0x0E, 0x24, 0x00,
  0x31, 0x0D, 0x00, 0x00, 0x00, 0x83, 0x33, 0x1D, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
  0x0A, 0x00, 0x2B, 0x1C, 0x3C, 0x94, 0xD5, 0x03, 0x08, 0x00, 0x00, 0x04, 0x93, 0x1E, 0x00, 0x82,
  0x23, 0x00, 0x74, 0x29, 0x00, 0x69, 0x2F, 0x00, 0x5F, 0x37, 0x00, 0x5F, 0x20, 0x40, 0x60, 0x00,
  0xF0, 0x40, 0x30, 0x55, 0x50, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x19, 0x00, 0x00, 0x50, 0x50,
  0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1D, 0x1E,
  0x1F, 0x20, 0x21, 0x22, 0x24, 0x26, 0x28, 0x29, 0x2A, 0x1C, 0x18, 0x16, 0x14, 0x13, 0x12, 0x10,
  0x0F, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x01,
};
// Bytes 184/185 of the table above are the original checksum/commit pair; they
// are recomputed in applyConfigVariant() because the switch/version bytes are
// edited there.

// Download one configuration variant. The reference block was captured on a
// landscape sensor (Module_Switch1 = 0x75, X2Y set). Here X_Max=720 /
// Y_Max=1280 describe the PANEL's native portrait frame - the same frame M5GFX
// configures this controller with (x_max=719, y_max=1279) and the one
// p4ui::touchPoint() expects - so a variant with X2Y cleared is worth trying.
// The checksum is recomputed rather than copied, and the block goes out in a
// single transaction with a raised TX buffer. The controller clears 0x8100 when
// it accepts, so that read-back is the acceptance verdict.
static bool applyConfigVariant(bool clearX2Y, uint8_t versionOverride,
                               uint8_t* checksumOut, uint8_t* freshOut) {
  uint8_t cfg[186];
  memcpy(cfg, GT911_REFERENCE_CONFIG, 184);
  if (clearX2Y) cfg[0x804D - 0x8047] &= (uint8_t)~0x04;
  if (versionOverride != 0xFF) cfg[0] = versionOverride;
  uint8_t sum = 0;
  for (int i = 0; i < 184; i++) sum = (uint8_t)(sum + cfg[i]);
  cfg[184] = (uint8_t)((uint8_t)(~sum) + 1);        // 0x80FF
  cfg[185] = 0x01;                                  // 0x8100 commit
  if (checksumOut) *checksumOut = cfg[184];

  // One transaction for the whole block. Some GT911 revisions refuse a block
  // that arrives split across several START/STOP pairs; Wire's default 128-byte
  // buffer is too small for 186 bytes, so touchInit() raises it first.
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)0x80);
  Wire.write((uint8_t)0x47);
  for (size_t i = 0; i < sizeof(cfg); i++) Wire.write(cfg[i]);
  const bool sent = (Wire.endTransmission(true) == 0);
  delay(150);
  uint8_t fresh = 0xFF;
  gt911Read(0x8100, &fresh, 1);
  if (freshOut) *freshOut = fresh;
  return sent;
}

static bool configLooksBlank() {
  uint8_t ver = 0, xy[2] = {0, 0};
  if (!gt911Read(0x8047, &ver, 1)) return false;
  if (!gt911Read(0x8048, xy, 2)) return false;
  return ver == 0 && xy[0] == 0 && xy[1] == 0;
}

// A blank configuration is recoverable: the controller accepts a download into
// 0x8047 and commits it when 0x8100 bit0 is set. Try the cheap recovery first
// (software reset -> reload from NVM), then the download.
static bool repairBlankConfig() {
  if (!configLooksBlank()) return true;      // already configured, leave it alone

  Serial.println("[gt911] configuration area is blank - trying a soft reset first");
  gt911Write(0x8040, 0x02);                  // CmdReg 2 = software reset
  delay(300);
  if (!configLooksBlank()) {
    Serial.println("[gt911] soft reset restored the stored configuration");
    return true;
  }

  Serial.println("[gt911] still blank - downloading the 720x1280 configuration");
  // Every variant is logged with the controller's own verdict (0x8100 clears on
  // acceptance). If it refuses the unmodified reference block AND the
  // version-0 form, the block is not what is wrong.
  static const struct { bool clearX2Y; uint8_t ver; const char* name; } variants[] = {
    { false, 0x42, "reference v0x42   " },
    { true,  0x42, "portrait  v0x42   " },
    { false, 0x00, "reference v0x00   " },
  };
  for (const auto& v : variants) {
    uint8_t cs = 0, fresh = 0xFF;
    if (!applyConfigVariant(v.clearX2Y, v.ver, &cs, &fresh)) {
      Serial.printf("[gt911] %s: write FAILED\n", v.name);
      continue;
    }
    Serial.printf("[gt911] %s: checksum=0x%02X 0x8100=0x%02X %s\n", v.name, cs, fresh,
                  fresh == 0x00 ? "ACCEPTED" : "rejected");
  }
  delay(100);
  uint8_t ver = 0, xy[2] = {0, 0}, sw1 = 0;
  gt911Read(0x8047, &ver, 1);
  gt911Read(0x8048, xy, 2);
  gt911Read(0x804D, &sw1, 1);
  const uint16_t xMax = (uint16_t)(xy[0] | (xy[1] << 8));
  Serial.printf("[gt911] final: version=0x%02X X_Max=%u switch1=0x%02X\n", ver, xMax, sw1);
  // CmdReg 2 (software reset) is what we used to force the NVM reload attempt;
  // leave the controller in normal command mode afterwards.
  gt911Write(0x8040, 0x00);
  gt911Write(GT911_REG_STATUS, 0);
  return ver != 0 && xMax != 0;
}

// Snapshot for the HTTP side: no bus access, so it is safe from any task.
TouchRegisterDump touchRegisterDump() {
  TouchRegisterDump d = dumpValid ? cachedDump : TouchRegisterDump{};
  d.polls = pollCount;
  d.status = lastStatusByte;
  d.intLevel = (GT911_INT >= 0) ? digitalRead(GT911_INT) : -1;
  return d;
}

// Documented GT911 wake sequence (the same one LovyanGFX issues right after a
// successful probe): INT is driven low for ~58 ms, released, then floats with a
// pull-up. Harmless when the controller is already awake, and the only way back
// out if it powered up asleep.
void touchWake() {
  if (!busReady) return;
  if (GT911_INT < 0) return;
  pinMode(GT911_INT, OUTPUT);
  digitalWrite(GT911_INT, LOW);
  delay(58);
  digitalWrite(GT911_INT, HIGH);
  delay(2);
  pinMode(GT911_INT, INPUT_PULLUP);
  Serial.println("GT911: wake pulse issued (INT low 58ms)");
}

// Called from touchPoll() when the controller stays silent. A controller that
// answers I2C yet publishes no frame is usually asleep, not broken: the INT-low
// pulse is the documented way back. Bounded to two attempts so a genuinely dead
// panel costs 2 x 58 ms and not a stall on every poll.
static void serviceSilentController() {
  if (frameCount || wakeAttempts >= 2) return;
  const uint32_t due = wakeAttempts ? 30000u : 8000u;   // polls at ~1 kHz
  if (pollCount < due) return;
  wakeAttempts++;
  dumpRegisters(wakeAttempts == 1 ? "no frames in 8s - waking" : "no frames in 30s - waking");
  touchWake();
  gt911Write(GT911_REG_STATUS, 0);
}

#endif  // USE_GT911
