// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

#include "display_p4.h"
#if defined(BOARD_IS_WS_P4_7B)
#include "display_ui.h"
#include "display_card.h"
#include "fonts.h"
#include "button_touch_backend.h"
#include "settings.h"
#include "bambu_mqtt.h"
#include "wifi_manager.h"
#include "buzzer.h"
#include "led.h"
#include "p4_camera_service.h"
#include "p4_runtime.h"
namespace {
P4DashboardView view;
p4ui::TouchTracker touch;
lgfx::LGFX_Sprite frame;
BambuState previous;
PrinterConfig previousConfig;
P4DashboardView previousView;
bool drawn = false;
uint8_t previousSlot = 255;
uint32_t lastDraw = 0;
time_t previousMinute = 0;
uint32_t lastInteraction = 0;
uint8_t previousRotation = 255;
uint32_t previousWifiState = 0xffffffff;
uint32_t lastProgressAnimation = 0;
uint32_t followedCameraGeneration = 0;
uint32_t previousCameraGeneration = 0;
}
bool p4DashboardActive() {
  const auto screen = getScreenState();
  return cardSkinActive() && isPrinterConfigured(rotState.displayIndex) &&
    (screen == SCREEN_PRINTING || screen == SCREEN_IDLE || screen == SCREEN_FINISHED);
}
int p4DashboardPage() { return view.page; }
uint8_t p4DashboardAmsPage() { return view.amsPage; }
bool p4DashboardShowingCamera() { return view.showCamera; }
// Animate only 656x8 pixels. No additional sprite, cloud connection, or
// full-frame redraw is needed for the moving highlight.
static uint32_t progressPhase(uint32_t now) {
  // Time-derived phase also survives telemetry repaints: they never restart
  // the highlight at the left edge. Four pixels per frame makes the narrow
  // highlight's motion readable on the physical panel without a full redraw.
  return now / 45 * 4;
}
bool tickP4Dashboard() {
  if (!drawn || view.page >= 0) return false;
  const auto& s = displayedPrinter().state;
  if (!s.connected || !s.printing || s.progress == 0 || s.progress > 100 ||
      !(s.p4Known & P4_PROGRESS)) return false;
  const uint32_t now = millis();
  if (now - lastProgressAnimation < 45) return false;
  lastProgressAnimation = now;
  // Partial refresh: the strip is repainted alone on top of the native frame,
  // so it has to be addressed in PANEL pixels - the same transform
  // drawP4Dashboard applies to the rest of the composition.
  drawP4Progress(*tft_ptr, p4ui::sx(p4ui::CONTENT), p4ui::sy(p4ui::PROGRESS_Y),
                 p4ui::slen(p4ui::RIGHT - p4ui::CONTENT),
                 s.progress, progressPhase(now), accentP4(s));
  return true;
}
void leaveP4Dashboard() {
  if (!drawn && !frame.getBuffer()) return;
  frame.setFont(nullptr); frame.deleteSprite();
  view = P4DashboardView{}; drawn = false; previousSlot = 255;
}
bool handleP4DashboardTouch() {
  const auto contact = touchContact();
  const bool enabled = buttonType == BTN_TOUCHSCREEN && p4DashboardActive() && !isNightBlackout();
  const auto point = p4ui::touchPoint(contact.x, contact.y, dispSettings.rotation);
#if defined(M5TAB5_TOUCH_TRACE)
  {
    static bool lastTraceDown = false;
    if (contact.down && !lastTraceDown) {
      // Raw pair is the panel frame; `point` is what the widgets are hit-tested
      // against. Tap the top-left corner: this must print canvas=(0..4,0..4).
      Serial.printf("[touch] canvas=(%d,%d) raw=(%u,%u) valid=%d rot=%u\n",
                    point.x, point.y, contact.x, contact.y, (int)contact.valid,
                    (unsigned)dispSettings.rotation);
    }
    lastTraceDown = contact.down;
  }
#endif
  // Header-icon press feedback. The Tab5 has no buzzer and no vibration motor,
  // so the only way to tell the user "your finger is on the gear" before the
  // page changes is to light the icon up on the glass. This runs ahead of the
  // `enabled` early-return so a finger that lifts mid-transition still clears
  // the tint instead of leaving it stuck on. Same 950/898 split as
  // p4DashboardGesture - keep the two in sync if the boxes ever move again.
  {
    static int8_t pressedIcon = 0;
    int8_t icon = 0;
    if (enabled && contact.down && contact.valid && point.y < 74) {
      if (point.x >= 950) icon = 2;
      else if (point.x >= 898) icon = 1;
      // The name row only switches printers from the home page; on a detail
      // page the same pixels do nothing, so they must not light up either.
      else if (point.x < 790 && view.page < 0) icon = 3;
    }
    if (icon != pressedIcon) {
      pressedIcon = icon;
      p4SetHeaderPress(icon);
      drawn = false;   // force updateP4Dashboard() to rasterise a fresh frame
    }
  }
  const auto gesture = touch.update(contact.down, contact.valid, point, millis(), enabled &&
    (previousSlot == rotState.displayIndex || previousSlot == 255) &&
    (previousRotation == dispSettings.rotation || previousRotation == 255));
  if (!enabled) return false;
  if (contact.down) {
    // Keep a manually explored printer visible through the gesture/detail read.
    rotState.lastRotateMs = millis();
    rotState.displayHoldUntilMs = millis() + 60000;
  }
  if (gesture != p4ui::Gesture::None) {
    buzzerPlayClick(); ledOnUserInteraction(); lastInteraction = millis();
    const auto action = p4DashboardGesture(view, displayedPrinter().state, gesture, touch.origin());
    if (action == P4Action::NextPrinter) {
      // A tap on the name row is an explicit choice: hold it far longer than
      // the 60 s "peek" a plain touch grants, otherwise Smart mode snaps the
      // view back to the printing slot a minute later and the switch looks
      // like it never took effect.
      constexpr uint32_t kManualSwitchHoldMs = 600000;   // 10 minutes
      for (uint8_t i=1;i<MAX_PRINTERS;++i) {
        const uint8_t next=(rotState.displayIndex+i)%MAX_PRINTERS;
        if (isPrinterConfigured(next)) {
          rotState.displayIndex=next; rotState.lastRotateMs=millis();
          rotState.displayHoldUntilMs=millis()+kManualSwitchHoldMs;
          view=P4DashboardView{}; drawn=false; triggerDisplayTransition();
          Serial.printf("[p4] tap-switch printer -> slot %u (%s)\n",
                        (unsigned)next, printers[next].config.name);
          break;
        }
      }
    }
  }
  return true;
}
bool updateP4Dashboard(bool force) {
  const uint32_t now=millis();
  if (previousSlot != rotState.displayIndex || previousRotation != dispSettings.rotation) {
    if (previousSlot != rotState.displayIndex) followedCameraGeneration = 0;
    view=P4DashboardView{}; drawn=false; previousSlot=rotState.displayIndex;
    previousRotation=dispSettings.rotation;
  }
  if (view.page>=0 && now-lastInteraction>60000) { view.page=-1; drawn=false; }
  const auto& p=displayedPrinter();
  followP4ActiveAms(view,p.state.ams);
  const auto pages=p4AmsPageCount(p.state.ams);
  if (view.amsPage>=pages) view.amsPage=0;
  const time_t wall=time(nullptr);
  P4NetworkStatus network{};
  p4NetworkSnapshot(network);
  const uint32_t wifiState = network.wifiState;
  uint32_t cameraGeneration=0;
#if defined(P4_CLOUD_CAMERA_EXPERIMENTAL)
  cameraGeneration=p4CameraGeneration();
  followP4CameraDefault(view.showCamera,view.cameraChosen,followedCameraGeneration,
                        cameraGeneration,p4CameraHasImage(p));
#endif
  // Transport timestamps are not visual changes. An identical delta report
  // should not decode artwork or repaint the entire display.
  BambuState snapshot=p.state;
  snapshot.lastUpdate=0; snapshot.lastPrintDataMs=0;
  const bool viewChanged=memcmp(&view,&previousView,sizeof(view))!=0;
  if (!force && drawn && !viewChanged && now-lastDraw<250) return false;
  if (!force && drawn && !memcmp(&snapshot,&previous,sizeof(snapshot)) &&
      !memcmp(&p.config,&previousConfig,sizeof(p.config)) &&
      !memcmp(&view,&previousView,sizeof(view)) && previousMinute==wall/60 && previousWifiState==wifiState &&
      previousCameraGeneration==cameraGeneration) return false;
  if (!frame.getBuffer()) {
    // The composition is rasterised into PSRAM first and then blitted 1:1 at
    // (0,0), so this sprite - not the panel - sets the visible area. At the
    // hardcoded 1024x600 it left a 256px and 120px black border on the Tab5
    // regardless of what p4ui scaled, because everything past the sprite edge
    // was simply never drawn.
    frame.setPsram(true); frame.setColorDepth(16);
    frame.createSprite(p4ui::W, p4ui::H);
  }
  setFont(tft,FONT_NONE);
  const char* ip = network.ip;
  auto& target=frame.getBuffer()?static_cast<lgfx::LovyanGFX&>(frame):tft;
  drawP4Dashboard(target,p,view,wall,ip,network.online,progressPhase(now));
  if (p.state.lastPrintDataMs) {
    static uint8_t firstFrameSlot = 255;
    if (firstFrameSlot != rotState.displayIndex) {
      firstFrameSlot = rotState.displayIndex;
      Serial.printf("[data-stage] slot=%u dashboard_frame state=%s progress=%u\n",
                    rotState.displayIndex, p.state.gcodeState, p.state.progress);
    }
  }
  if (frame.getBuffer()) frame.pushSprite(tft_ptr,0,0);
  markFrameDirty();
  previous=snapshot; previousConfig=p.config; previousView=view;
  previousMinute=wall/60; lastDraw=now; drawn=true;
  previousWifiState=wifiState;
  previousCameraGeneration=cameraGeneration;
  return true;
}
#endif
