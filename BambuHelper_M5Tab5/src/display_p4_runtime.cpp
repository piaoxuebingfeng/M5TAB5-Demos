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
  drawP4Progress(*tft_ptr, p4ui::CONTENT, p4ui::PROGRESS_Y, p4ui::RIGHT - p4ui::CONTENT,
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
      for (uint8_t i=1;i<MAX_PRINTERS;++i) {
        const uint8_t next=(rotState.displayIndex+i)%MAX_PRINTERS;
        if (isPrinterConfigured(next)) {
          rotState.displayIndex=next; rotState.lastRotateMs=millis();
          view=P4DashboardView{}; drawn=false; triggerDisplayTransition(); break;
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
    frame.setPsram(true); frame.setColorDepth(16); frame.createSprite(1024,600);
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
