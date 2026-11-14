#pragma once
#include <cstdint>
#if defined(BOARD_IS_WS_P4_7B) && defined(P4_CLOUD_CAMERA_EXPERIMENTAL)
#include "LovyanGFX.hpp"
#include <vector>
#include <ctime>
#include "bambu_state.h"
struct P4CameraTiming {
  time_t capturedAt = 0;
  time_t nextUpdateAt = 0;
  bool busy = false;
  bool automatic = false;
  char blockedBy[24]{};
};
void initP4CameraService();
void registerP4CameraRoutes();
void startP4CameraService();
void tickP4CameraService();
void p4CameraVisibility(uint8_t slot, bool visible);
uint32_t p4CameraGeneration();
bool drawP4Camera(lgfx::LovyanGFX& target, const PrinterSlot& printer, int x, int y);
bool p4CameraJobName(const PrinterSlot& printer, char* out, size_t capacity);
bool p4CameraBusy();
bool p4CameraHasImage(const PrinterSlot& printer);
void p4CameraTiming(const PrinterSlot& printer, P4CameraTiming& timing);
void followP4CameraDefault(bool& showCamera, bool& imageChosen,
                           uint32_t& lastGeneration, uint32_t generation,
                           bool hasCurrentImage);
#if defined(P4_PREVIEW)
#include "p4_dashboard_layout.h"
constexpr uint16_t p4PreviewCameraPaper = (p4ui::PANEL_BACKGROUND >> 8) | (p4ui::PANEL_BACKGROUND << 8);
inline std::vector<uint16_t> p4PreviewCamera(p4ui::IMAGE_W * p4ui::IMAGE_CONTENT_H, p4PreviewCameraPaper);
inline bool p4CameraBusy() { return false; }
inline bool p4CameraHasImage(const PrinterSlot&) { return true; }
inline void p4CameraTiming(const PrinterSlot&, P4CameraTiming& timing) {
  timing = {};
  timing.capturedAt = 1792937702;
  timing.nextUpdateAt = 1792937762;
  timing.automatic = true;
}
inline void followP4CameraDefault(bool& showCamera, bool& imageChosen,
                                  uint32_t& lastGeneration, uint32_t generation,
                                  bool hasCurrentImage) {
  if (lastGeneration == generation) return;
  lastGeneration = generation;
  if (!imageChosen && hasCurrentImage) showCamera = true;
}
inline uint32_t p4CameraGeneration() { return 0; }
inline bool p4CameraJobName(const PrinterSlot&, char* out, size_t capacity) {
  if (out && capacity) out[0] = 0;
  return false;
}
inline bool drawP4Camera(lgfx::LovyanGFX& target, const PrinterSlot&, int x, int y) {
  // Synthetic 16:9 content, including all four corners; useful for proving
  // that the renderer does not crop the source or move the rest of the page.
  std::fill(p4PreviewCamera.begin(),p4PreviewCamera.end(),p4PreviewCameraPaper);
  for (int row = 0; row < p4ui::CAMERA_H; ++row) {
    for (int col = 0; col < p4ui::IMAGE_W; ++col) {
      p4PreviewCamera[(row+p4ui::CAMERA_OFFSET_Y) * p4ui::IMAGE_W + col] = ((col / 26 + row / 18) & 1) ? 0x001f : 0x07e0;
    }
  }
  target.pushImage(x, y, p4ui::IMAGE_W, p4ui::IMAGE_CONTENT_H, p4PreviewCamera.data());
  return true;
}
#endif
#else
inline void initP4CameraService() {}
inline void registerP4CameraRoutes() {}
inline void startP4CameraService() {}
inline void tickP4CameraService() {}
inline void p4CameraVisibility(uint8_t, bool) {}
inline void followP4CameraDefault(bool&, bool&, uint32_t&, uint32_t, bool) {}
#endif
