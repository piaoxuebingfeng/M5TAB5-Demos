#pragma once

#if defined(BOARD_IS_WS_P4_7B)
#include "LovyanGFX.hpp"
#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_ldo_regulator.h>

namespace lgfx { inline namespace v1 {

// Native RGB565 framebuffer; LovyanGFX supplies clipping, rotation and fonts.
// Timings and EK79007 registers match Waveshare's LCD-7B Arduino/BSP profile.
class Panel_EK79007_7B : public Panel_FrameBufferBase {
public:
  Panel_EK79007_7B() {
    _cfg.panel_width = _cfg.memory_width = 1024;
    _cfg.panel_height = _cfg.memory_height = 600;
    _cfg.offset_rotation = 2;  // LCD-7B mounting: rotate the dashboard 180 degrees
    _cfg.pin_rst = -1;  // reset is sequenced before sending DSI commands
    _write_depth = _read_depth = color_depth_t::rgb565_nonswapped;
  }

  color_depth_t setColorDepth(color_depth_t) override {
    return Panel_FrameBufferBase::setColorDepth(color_depth_t::rgb565_nonswapped);
  }

  void setInvert(bool invert) override {
    _invert = invert;
    if (_io) check(esp_lcd_panel_io_tx_param(_io, invert ? 0x21 : 0x20, nullptr, 0), "inversion");
  }

  bool init(bool) override {
    if (_lines_buffer) return true;
    ::pinMode(32, OUTPUT);
    digitalWrite(32, HIGH);  // active-low backlight: keep dark during bring-up
    ::pinMode(33, OUTPUT);
    digitalWrite(33, LOW);
    delay(10);
    digitalWrite(33, HIGH);
    delay(20);

    esp_ldo_channel_config_t ldo = {};
    ldo.chan_id = 3;
    ldo.voltage_mv = 2500;
    if (!check(esp_ldo_acquire_channel(&ldo, &_ldo), "MIPI LDO")) return false;

    esp_lcd_dsi_bus_config_t bus = {};
    bus.bus_id = 0;
    bus.num_data_lanes = 2;
    // ESP-IDF chooses the PHY source for the selected P4 silicon revision.
    bus.phy_clk_src = static_cast<mipi_dsi_phy_pllref_clock_source_t>(0);
    bus.lane_bit_rate_mbps = 1000;
    if (!check(esp_lcd_new_dsi_bus(&bus, &_dsi), "DSI bus")) return false;

    esp_lcd_dbi_io_config_t dbi = {};
    dbi.lcd_cmd_bits = dbi.lcd_param_bits = 8;
    if (!check(esp_lcd_new_panel_io_dbi(_dsi, &dbi, &_io), "DSI IO")) return false;

    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi.dpi_clock_freq_mhz = 52;
    dpi.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi.num_fbs = 1;
    dpi.video_timing.h_size = 1024;
    dpi.video_timing.v_size = 600;
    dpi.video_timing.hsync_pulse_width = 10;
    dpi.video_timing.hsync_back_porch = 160;
    dpi.video_timing.hsync_front_porch = 160;
    dpi.video_timing.vsync_pulse_width = 1;
    dpi.video_timing.vsync_back_porch = 23;
    dpi.video_timing.vsync_front_porch = 12;
    if (!check(esp_lcd_new_panel_dpi(_dsi, &dpi, &_dpi), "DPI panel")) return false;

    static const uint8_t commands[][2] = {
      {0xB2, 0x10}, {0x80, 0x8B}, {0x81, 0x78}, {0x82, 0x84},
      {0x83, 0x88}, {0x84, 0xA8}, {0x85, 0xE3}, {0x86, 0x88}
    };
    for (const auto& cmd : commands) {
      if (!check(esp_lcd_panel_io_tx_param(_io, cmd[0], &cmd[1], 1), "EK79007 init")) return false;
    }
    if (!check(esp_lcd_panel_io_tx_param(_io, 0x11, nullptr, 0), "sleep out")) return false;
    delay(120);

    void* fb = nullptr;
    if (!check(esp_lcd_dpi_panel_get_frame_buffer(_dpi, 1, &fb), "framebuffer")) return false;
    memset(fb, 0, 1024u * 600u * 2u);
    for (size_t y = 0; y < 600; ++y) _rows[y] = static_cast<uint8_t*>(fb) + y * 1024u * 2u;
    _lines_buffer = _rows;
    if (!Panel_FrameBufferBase::init(false)) return false;
    // Cache writeback is batched by flushFrame() at the end of each UI tick.
    _auto_display = false;
    display(0, 0, 1024, 600);
    return check(esp_lcd_panel_init(_dpi), "DPI start");
  }

private:
  esp_ldo_channel_handle_t _ldo = nullptr;
  esp_lcd_dsi_bus_handle_t _dsi = nullptr;
  esp_lcd_panel_io_handle_t _io = nullptr;
  esp_lcd_panel_handle_t _dpi = nullptr;
  uint8_t* _rows[600] = {};

  static bool check(esp_err_t err, const char* step) {
    if (err == ESP_OK) return true;
    Serial.printf("LCD-7B: %s failed: %s\n", step, esp_err_to_name(err));
    return false;
  }
};

} }
#endif
