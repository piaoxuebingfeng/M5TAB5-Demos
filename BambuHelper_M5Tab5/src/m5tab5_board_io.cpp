#include "board_config.h"

#include "m5tab5_board_io.h"

#if defined(BOARD_IS_M5TAB5)

#include <Wire.h>

namespace {

constexpr uint8_t IO1 = 0x43;   // LCD / touch / camera resets, speaker, RF
constexpr uint8_t IO2 = 0x44;   // C6 power, USB-A, charger

// PI4IOE5V6408 register map (only the ones used here).
constexpr uint8_t REG_INPUT   = 0x01;
constexpr uint8_t REG_IO_DIR  = 0x03;   // 1 = input, 0 = output
constexpr uint8_t REG_OUT_SET = 0x05;
constexpr uint8_t REG_OUT_HIM = 0x07;   // 1 = high-impedance
constexpr uint8_t REG_IN_DEF  = 0x09;
constexpr uint8_t REG_PULL_EN = 0x0B;
constexpr uint8_t REG_PULL_SEL = 0x0D;  // 1 = pull-up
constexpr uint8_t REG_INT_MASK = 0x11;

// --- Shadow state ---------------------------------------------------------
// Both chips latch their output register as a whole byte, so an uncoordinated
// bit write would clobber the resets. Everything goes through these shadows.
uint8_t outIo1 = 0;
uint8_t outIo2 = 0;
bool    ready  = false;

bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readReg(uint8_t addr, uint8_t reg, uint8_t* value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, 1) != 1) return false;
  *value = (uint8_t)Wire.read();
  return true;
}

// Apply a table of (reg, value) pairs, stopping at the 0xFF sentinel.
bool writeTable(uint8_t addr, const uint8_t* table) {
  for (size_t i = 0; table[i] != 0xFF; i += 2) {
    if (!writeReg(addr, table[i], table[i + 1])) return false;
  }
  return true;
}

// Read-modify-write one bit of an output shadow.
void setOutputBit(uint8_t addr, uint8_t& shadow, uint8_t bit, bool high) {
  const uint8_t mask = (uint8_t)(1u << bit);
  uint8_t next = high ? (uint8_t)(shadow | mask) : (uint8_t)(shadow & ~mask);
  if (next == shadow) return;
  if (!writeReg(addr, REG_OUT_SET, next)) return;
  shadow = next;
}

}  // namespace

bool m5tab5IoBegin() {
  if (ready) return true;

  // The expanders share the system I2C bus with the touch controller, the
  // IMU, the RTC, the fuel gauge and the codecs, so the bus is brought up
  // here once and then handed to the rest of the firmware at 400 kHz.
  Wire.begin(M5TAB5_TOUCH_SDA, M5TAB5_TOUCH_SCL);
  Wire.setClock(100000);

  // GT911 latches its address during the TP_RST release below: high = 0x14.
  pinMode(M5TAB5_TOUCH_INT, OUTPUT);
  digitalWrite(M5TAB5_TOUCH_INT, HIGH);

  // --- PI4IOE-1: assert LCD_RST + TP_RST, camera released ---------------
  static const uint8_t io1Reset[] = {
    REG_IO_DIR,   0b01111111,   // P0..P6 outputs, P7 input
    REG_OUT_SET,  0b01000110,   // LCD_RST(P4) + TP_RST(P5) LOW, CAM_RST(P6) HIGH
    REG_OUT_HIM,  0b00000000,
    REG_PULL_SEL, 0b01111111,
    REG_PULL_EN,  0b01111111,
    0xFF, 0xFF,
  };
  // --- PI4IOE-2: C6 rail on, charger defaults --------------------------
  static const uint8_t io2Init[] = {
    REG_IO_DIR,   0b10111001,   // P0/P3/P4/P5/P7 input, P1/P2/P6 output
    REG_OUT_HIM,  0b00000110,
    REG_PULL_SEL, 0b10111001,   // pull-ups on the input pins
    REG_PULL_EN,  0b11111001,   // ... including P0 = WLAN_PWR_EN
    REG_IN_DEF,   0b01000000,
    REG_INT_MASK, 0b10111111,
    REG_OUT_SET,  0b10001001,   // P0 high through the pull-up, P3/P7 high
    0xFF, 0xFF,
  };

  const bool ok1 = writeTable(IO1, io1Reset);
  const bool ok2 = writeTable(IO2, io2Init);
  if (!ok1 || !ok2) {
    Serial.printf("[tab5-io] PI4IOE missing (0x43=%d 0x44=%d)\n", ok1 ? 1 : 0, ok2 ? 1 : 0);
    return false;
  }
  outIo1 = 0b01000110;
  outIo2 = 0b10001001;

  delay(10);

  // --- release both resets ---------------------------------------------
  static const uint8_t io1Release[] = {
    REG_OUT_SET, 0b01110110,   // LCD_RST and TP_RST HIGH
    0xFF, 0xFF,
  };
  if (!writeTable(IO1, io1Release)) return false;
  outIo1 = 0b01110110;

  // Address is latched now; TP_INT becomes the GT911 interrupt line.
  pinMode(M5TAB5_TOUCH_INT, INPUT);
  delay(100);   // panel + touch boot

  Wire.setClock(400000);
  ready = true;
  Serial.println("[tab5-io] PI4IOE 0x43/0x44 ready, LCD_RST + TP_RST released");
  return true;
}

bool m5tab5IoReady() { return ready; }

void m5tab5SpeakerEnable(bool on) {
  if (!ready) return;
  setOutputBit(IO1, outIo1, 2, on);
}

void m5tab5CameraReset(bool release) {
  if (!ready) return;
  setOutputBit(IO1, outIo1, 6, release);
}

// TP_RST lives on PI4IOE-1 P5. The display bring-up pulses it as well, but that
// happens ~2 s before the touch driver probes; an independent pulse here is what
// guarantees the controller is in a known power-up mode at probe time.
void m5tab5TouchReset(bool intHigh) {
  if (!ready) return;
  pinMode(M5TAB5_TOUCH_INT, OUTPUT);
  digitalWrite(M5TAB5_TOUCH_INT, intHigh ? HIGH : LOW);
  delay(2);
  setOutputBit(IO1, outIo1, M5TAB5_IO1_TP_RST, false);   // assert reset
  delay(12);
  setOutputBit(IO1, outIo1, M5TAB5_IO1_TP_RST, true);    // release; INT latched
  delay(120);                                            // GT911 boot
  pinMode(M5TAB5_TOUCH_INT, INPUT_PULLUP);
}

void m5tab5RadioPower(bool on) {  if (!ready) return;
  if (on) {
    // Same state M5GFX leaves the C6 rail in: output register high, pin used
    // as an input with a pull-up so the rail is enabled without back-driving.
    writeReg(IO2, REG_OUT_SET, (uint8_t)(outIo2 | 0x01));
    uint8_t dir = 0, pull = 0, sel = 0;
    if (readReg(IO2, REG_IO_DIR, &dir) && readReg(IO2, REG_PULL_EN, &pull) &&
        readReg(IO2, REG_PULL_SEL, &sel)) {
      writeReg(IO2, REG_IO_DIR, (uint8_t)(dir | 0x01));
      writeReg(IO2, REG_PULL_EN, (uint8_t)(pull | 0x01));
      writeReg(IO2, REG_PULL_SEL, (uint8_t)(sel | 0x01));
    }
  } else {
    writeReg(IO2, REG_OUT_SET, (uint8_t)(outIo2 & ~0x01));
    uint8_t dir = 0;
    if (readReg(IO2, REG_IO_DIR, &dir)) {
      writeReg(IO2, REG_IO_DIR, (uint8_t)(dir & ~0x01));
    }
  }
}

// INA226 @0x41: bus voltage 1.25 mV/LSB, shunt voltage 2.5 uV/LSB (signed).
// The part boots in continuous shunt+bus conversion, so no configuration write
// is needed - which matters, because this is read before anything else runs.
bool m5tab5ReadPower(float* busVolts, float* shuntMillivolts) {
  constexpr uint8_t INA226 = 0x41;
  auto readWord = [](uint8_t addr, uint8_t reg, uint16_t* out) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, 2) != 2) return false;
    const uint8_t hi = (uint8_t)Wire.read();
    const uint8_t lo = (uint8_t)Wire.read();
    *out = (uint16_t)((hi << 8) | lo);
    return true;
  };
  uint16_t bus = 0, shunt = 0;
  if (!readWord(INA226, 0x02, &bus)) return false;
  if (!readWord(INA226, 0x01, &shunt)) return false;
  if (busVolts) *busVolts = bus * 1.25f / 1000.0f;
  if (shuntMillivolts) *shuntMillivolts = (int16_t)shunt * 0.0025f;
  return true;
}

#endif  // BOARD_IS_M5TAB5