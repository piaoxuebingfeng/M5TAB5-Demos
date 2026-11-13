#pragma once

static constexpr unsigned char normalizeDisplayRotation(unsigned char rotation) {
#if defined(BOARD_IS_WS_P4_7B)
  return rotation & 2;  // the seven-inch dashboard uses landscape at 0/180
#else
  return rotation;
#endif
}

#if defined(BOARD_IS_WS_P4_7B)
// Only unassigned GPIOs below the separately powered VO4 bank. Reserve the
// onboard audio (9-13/53), C6 SDIO (14-19/54), CAN (21/22), RS485 (26/27),
// touch (7/8), LCD (32/33), USB, UART, boot straps and SD/flash/PSRAM banks.
// The helper is shared by LED, button and buzzer validation, including NVS.
static constexpr bool isP4SevenBReservedPin(int pin) {
  return !((pin >= 1 && pin <= 6) || pin == 20 || pin == 23 ||
           pin == 24 || pin == 25 || pin == 28 || pin == 29 ||
           pin == 30 || pin == 31 || pin == 34);
}
#endif
