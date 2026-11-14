// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

#include "p4_runtime.h"
#if defined(BOARD_IS_WS_P4_7B)
#include "display_p4.h"
#include "display_ui.h"
#include "settings.h"
#include "p4_camera_service.h"
#include <atomic>
#include <cstring>
#include <new>
#include <esp_heap_caps.h>
namespace {
struct Frame {
  uint16_t pixels[1024 * 600];
  int width = 0, height = 0;
};
using FrameMailbox = SpscLatest<Frame, 2>;
std::atomic<FrameMailbox*> frames{nullptr};
Frame* frameForHttp = nullptr;
SpscLatest<TouchDiagnostics> touchDiagnosticsForHttp;
SpscLatest<TouchContact> touchContactForHttp;
QueueHandle_t screenCommands = nullptr, amsCommands = nullptr;
std::atomic<bool> repaintPending{false};
std::atomic<uint32_t> captureSerial{0}, capturedSerial{0};
P4UiStatus status;
}

void p4UiServiceStart() {
  // Screenshot buffers are allocated by the HTTP owner on first request.
  // The normal dashboard should not reserve four full 1024x600 frames.
}

void p4UiPublishStatus() {
  struct CaptureGuard {
    ~CaptureGuard() { screenCaptureEnd(); }
  };
  if (repaintPending.exchange(false, std::memory_order_acq_rel)) triggerDisplayTransition();
  uint8_t screen = 0;
  if (screenCommands && xQueueReceive(screenCommands, &screen, 0) == pdPASS)
    setScreenState(ScreenState(screen));
  bool ams = false;
  if (amsCommands && xQueueReceive(amsCommands, &ams, 0) == pdPASS && ams) toggleAmsPage();
  const P4UiStatus previous = status;
  status.screen = getScreenState(); status.page = p4DashboardPage();
  status.amsPage = p4DashboardAmsPage(); status.dashboard = p4DashboardActive();
  status.nightBlackout = isNightBlackout();
  status.cameraImage = p4DashboardShowingCamera();
  status.slot = rotState.displayIndex;
  static uint32_t lastStackSample = 0;
  const uint32_t now = millis();
  static uint32_t lastRepublish = 0;
  const bool refresh = now - lastRepublish >= 1000;
  if (!status.loopStackMin || now - lastStackSample >= 1000) {
    status.loopStackMin = p4UiLoopStackMinWords();
    lastStackSample = now;
  }
  static bool statusPublished = false;
  if (!statusPublished || refresh || previous.screen != status.screen || previous.page != status.page ||
      previous.amsPage != status.amsPage || previous.loopStackMin != status.loopStackMin ||
      previous.dashboard != status.dashboard || previous.nightBlackout != status.nightBlackout ||
      previous.cameraImage != status.cameraImage || previous.slot != status.slot) {
    p4PublishUiStatus(status);
    statusPublished = true;
  }
  const TouchDiagnostics diagnostics = touchDiagnostics();
  const TouchContact contact = touchContact();
  static TouchDiagnostics previousDiagnostics{};
  static TouchContact previousContact{};
  static bool touchPublished = false;
  if (!touchPublished || refresh || diagnostics.ready != previousDiagnostics.ready ||
      diagnostics.down != previousDiagnostics.down || diagnostics.frames != previousDiagnostics.frames ||
      diagnostics.presses != previousDiagnostics.presses || diagnostics.readErrors != previousDiagnostics.readErrors) {
    touchDiagnosticsForHttp.update(diagnostics);
    previousDiagnostics = diagnostics;
  }
  if (!touchPublished || refresh || contact.down != previousContact.down || contact.valid != previousContact.valid ||
      contact.x != previousContact.x || contact.y != previousContact.y) {
    touchContactForHttp.update(contact);
    previousContact = contact;
  }
  touchPublished = true;
  if (refresh) lastRepublish = now;

  const bool visible = status.dashboard && status.page == 255 && !status.nightBlackout;
  static uint8_t lastSlot = 255; static bool lastVisible = false;
  if (lastSlot != rotState.displayIndex || lastVisible != visible) {
    lastSlot = rotState.displayIndex; lastVisible = visible;
    p4CameraVisibility(lastSlot, visible);
  }

  // A screenshot is diagnostic work, not another continuous display pipeline.
  // Wait until after the real panel has been painted, and only then capture
  // one frame requested by the HTTP task.
  FrameMailbox* mailbox = frames.load(std::memory_order_acquire);
  const uint32_t request = captureSerial.load(std::memory_order_acquire);
  if (!mailbox || capturedSerial.load(std::memory_order_acquire) == request) return;
  const uint16_t* pixels = nullptr; int16_t w = 0, h = 0;
  const bool begun = screenCaptureBegin(&pixels, &w, &h);
  CaptureGuard capture;
  if (!begun) return;
  // screenCaptureBegin can repaint a legacy view while assembling its shot.
  // Always release its sprite and restore the real panel, including a frame
  // whose size cannot be published or whose mailbox is temporarily occupied.
  if (!pixels || w <= 0 || h <= 0 || w > 1024 || h > 600 ||
      size_t(w) * h > sizeof(Frame::pixels) / sizeof(uint16_t)) {
    Serial.printf("P4 screenshot: invalid dimensions %d x %d\n", int(w), int(h));
    return;
  }
  if (Frame* frame = mailbox->beginWrite()) {
    memcpy(frame->pixels, pixels, size_t(w) * h * sizeof(uint16_t));
    frame->width = w; frame->height = h;
    mailbox->publish();
    // A newer request arriving during this copy needs its own fresh frame.
    capturedSerial.store(request, std::memory_order_release);
  }
}

void p4UiRepaint() { repaintPending.store(true, std::memory_order_release); }
ScreenState p4WebScreenState() {
  P4UiStatus current;
  return p4HttpUiSnapshot(current) ? ScreenState(current.screen) : SCREEN_IDLE;
}
bool p4WebNightBlackout() {
  P4UiStatus current;
  return p4HttpUiSnapshot(current) && current.nightBlackout;
}
bool p4UiToggleAms() {
  P4UiStatus current;
  if (!p4HttpUiSnapshot(current)) return false;
  if (current.screen != SCREEN_AMS) {
    if (current.screen != SCREEN_IDLE && current.screen != SCREEN_PRINTING &&
        current.screen != SCREEN_FINISHED) return false;
    BambuState state;
    PrinterConfig config;
    if (!p4HttpPrinterSnapshot(current.slot, config, state) || !state.connected ||
        !((state.ams.present && state.ams.unitCount > 0) || state.ams.vtPresent)) return false;
  }
  if (!amsCommands) amsCommands = xQueueCreate(4, sizeof(uint8_t));
  const uint8_t value = 1;
  return amsCommands && xQueueSend(amsCommands, &value, 0) == pdPASS;
}
void p4UiSetScreen(uint8_t screen) {
  if (!screenCommands) screenCommands = xQueueCreate(4, sizeof(uint8_t));
  xQueueSend(screenCommands, &screen, 0);
}
bool p4UiCopyFrame(const uint16_t*& pixels, int& width, int& height) {
  pixels = nullptr; width = height = 0;
  FrameMailbox* mailbox = frames.load(std::memory_order_acquire);
  if (!mailbox || !frameForHttp) return false;
  // HTTP owns this PSRAM send buffer. Do not put a 1.2MB temporary on its
  // 32KB task stack or bounce the frame through another large mailbox.
  Frame* stable = frameForHttp;
  if (!mailbox->copyLatest(*stable)) return false;
  pixels = stable->pixels; width = stable->width; height = stable->height;
  return true;
}
uint32_t p4UiRequestFrame() {
  // The web server has one owner. Allocate both the send buffer and mailbox
  // here, before release-publishing the mailbox to the UI, never on loopTask.
  if (!frames.load(std::memory_order_acquire)) {
    auto* sendBuffer = static_cast<Frame*>(heap_caps_calloc(1, sizeof(Frame),
                                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!sendBuffer) return 0;
    FrameMailbox* mailbox = FrameMailbox::createExternal();
    if (!mailbox) { heap_caps_free(sendBuffer); return 0; }
    frameForHttp = sendBuffer;
    frames.store(mailbox, std::memory_order_release);
  }
  const uint32_t request = captureSerial.fetch_add(1, std::memory_order_acq_rel) + 1;
  return request == 0 ? captureSerial.fetch_add(1, std::memory_order_acq_rel) + 1 : request;
}
bool p4UiFrameReady(uint32_t request) {
  return request && static_cast<int32_t>(capturedSerial.load(std::memory_order_acquire) - request) >= 0;
}
bool p4HttpTouchSnapshot(TouchDiagnostics& diagnostics, TouchContact& contact) {
  return touchDiagnosticsForHttp.copyLatest(diagnostics) &&
         touchContactForHttp.copyLatest(contact);
}
bool p4UiCameraImage(uint8_t slot) {
  P4UiStatus current;
  return p4HttpUiSnapshot(current) && current.cameraImage && current.slot == slot;
}
#endif
