#pragma once

// =============================================================================
//  LovyanGFX.hpp compatibility shim (Arduino IDE build of BambuHelper)
// =============================================================================
//
// PlatformIO pulled the standalone `lovyan03/LovyanGFX@1.2.24` package into
// libdeps and BambuHelper compiled against it. Under Arduino IDE we instead
// use the locally installed M5GFX, which vendors its own copy of the *same*
// lgfx:: namespace (v1.2.x). Two copies of `namespace lgfx` cannot coexist:
// every class, enum and inline function would be defined twice in one
// translation unit.
//
// So rather than installing a second LovyanGFX, this header simply forwards to
// M5GFX. The subset BambuHelper relies on is available there:
//   lgfx::LovyanGFX, lgfx::LGFX_Device, lgfx::LGFX_Sprite,
//   lgfx::Panel_FrameBufferBase, lgfx::IFont, lgfx::VLWfont,
//   lgfx::PointerWrapper, lgfx::fonts::efontCN_24, lgfx::lgfx_qrcode_*
//
// M5GFX additionally ships the Tab5 MIPI-DSI stack that standalone LovyanGFX
// lacks: lgfx/v1/platforms/esp32p4/{Bus_DSI,Panel_DSI,Panel_ILI9881C,
// Panel_ST7123,Panel_ST7121,Touch_ST7123}. That is what actually lights up
// this board, whose 2025-06-30 first-batch unit carries ILI9881C + GT911.
//
// Keep this file at the sketch `src/` root and include it with quotes
// ("LovyanGFX.hpp") - Arduino does not put src/ on the angle-bracket path.

#include <M5GFX.h>

// lgfx_qrcode_* lives in a separate utility header upstream. BambuHelper's
// P4 settings page draws a QR code, so pull it in here; it is a C header and
// safe to include from C++.
#include <lgfx/utility/lgfx_qrcode.h>
