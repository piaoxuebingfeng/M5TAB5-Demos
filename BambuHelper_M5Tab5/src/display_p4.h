#pragma once
#if defined(BOARD_IS_WS_P4_7B)
#include "LovyanGFX.hpp"
#include <time.h>
#include "bambu_state.h"
#include "p4_dashboard_layout.h"

struct P4DashboardView {
  int8_t page = -1; // -1 home; 0 job, 1 filament, 2 device, 3 alerts, 4 settings
  uint8_t amsPage = 0;
  uint8_t alertOffset = 0;
  bool showCamera = false;
  bool cameraChosen = false;
  bool amsManual = false;
};
enum class P4Action { None, NextPrinter };
P4Action p4DashboardGesture(P4DashboardView& view, const BambuState& state,
                            p4ui::Gesture gesture, p4ui::Point origin);
uint16_t accentP4(const BambuState& state);
// Replace only the 8px progress strip during animation; the full native
// framebuffer remains intact. This adds no TLS session or extra frame buffer.
void drawP4Progress(lgfx::LovyanGFX& canvas, int x, int y, int width,
                    uint8_t percent, uint32_t phase, uint16_t accent);
uint8_t p4AmsPageCount(const AmsState& ams);
void followP4ActiveAms(P4DashboardView& view,const AmsState& ams);
void drawP4Dashboard(lgfx::LovyanGFX& canvas, const PrinterSlot& printer,
                     const P4DashboardView& view, time_t now, const char* deviceIp,
                     bool networkOnline, uint32_t progressPhase = 0);
// Integration owns screen eligibility, input capture and redraw scheduling.
bool p4DashboardActive();
bool handleP4DashboardTouch();
bool updateP4Dashboard(bool force); // true only when a full frame was repainted
void leaveP4Dashboard();
int p4DashboardPage(); // -1 home, otherwise the detail tab; diagnostics only
uint8_t p4DashboardAmsPage();
bool p4DashboardShowingCamera();
bool tickP4Dashboard();
#endif
