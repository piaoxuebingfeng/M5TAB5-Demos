#ifndef M5TAB5_BOARD_IO_H
#define M5TAB5_BOARD_IO_H

#include <stdint.h>

// =============================================================================
//  M5Stack Tab5 board power / reset sequencing
// =============================================================================
//
// Tab5 routes every rail and reset that matters through two PI4IOE5V6408 I2C
// expanders on the system bus (SDA=G31, SCL=G32):
//
//   PI4IOE5V6408-1 @0x43   P0 RF antenna path   P1 RF_INT_EXT_SWITCH
//                          P2 NS4150B SPK_EN    P4 LCD_RST
//                          P5 TP_RST            P6 CAM_RST
//   PI4IOE5V6408-2 @0x44   P0 WLAN_PWR_EN (C6)  P3 USB-A 5V enable
//                          P4 PWROFF_PLUSE      P5 IP2326 nCHG_QC_EN
//                          P6 CHG_STAT_LED      P7 CHG_EN
//
// Two things make this ordering matter:
//   * the MIPI-DSI PHY will not answer until LCD_RST has been released, so the
//     pulse has to happen before lgfx::LGFX_Device::init();
//   * the GT911 latches its I2C address from the level on TP_INT while TP_RST
//     is released - high selects 0x14 (the address the Tab5 ships with), low
//     selects 0x5D. Driving TP_INT after tft.init() is too late.
//
// The register tables below are copied verbatim from M5GFX's own Tab5
// autodetect path (M5GFX.cpp, board_M5Tab5 branch) so this build brings the
// hardware up exactly the way M5Unified would.

// Runs the PI4IOE reset/power sequence and leaves TP_INT as an input.
// Idempotent - later calls are cheap no-ops. Returns false if either expander
// failed to acknowledge.
bool m5tab5IoBegin();

// True once m5tab5IoBegin() has acknowledged both chips.
bool m5tab5IoReady();

// Individual control lines. All of them no-op until m5tab5IoBegin() succeeded.
void m5tab5SpeakerEnable(bool on);     // 0x43 P2, NS4150B amplifier
void m5tab5CameraReset(bool release);  // 0x43 P6, active low (left released)
void m5tab5RadioPower(bool on);        // 0x44 P0, ESP32-C6 supply
// Pulse TP_RST through PI4IOE-1 P5 with a defined level on TP_INT, which is
// what the GT911 latches its I2C address (and its power-up mode) from:
// intHigh = true -> 0x14, intHigh = false -> 0x5D.
void m5tab5TouchReset(bool intHigh);

// INA226 @0x41 on the same system bus, wired across the battery input. Used
// for brown-out forensics: the Tab5 can drop out seconds into boot, before the
// application can report anything, so the caller needs a reading it can print
// straight to the UART. Returns false when the part does not acknowledge.
bool m5tab5ReadPower(float* busVolts, float* shuntMillivolts);

#endif  // M5TAB5_BOARD_IO_H
