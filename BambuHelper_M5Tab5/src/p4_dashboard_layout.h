#pragma once
#include <stdint.h>

// Native 7B canvas. Shared by drawing and hit testing, including host previews.
namespace p4ui {
constexpr uint16_t PANEL_BACKGROUND = 0xef7e;  // #eceff1 in RGB565.

// Every coordinate in this file is authored against the 7B's 1024x600 canvas.
// The Tab5 panel is 1280x720, so instead of re-authoring ~200 hand-tuned pixel
// positions we scale the whole composition once, at the draw call boundary
// (see ScaledGfx in display_p4.cpp). 1024*6/5 = 1228, which leaves 26px of
// same-colour background on each side - visually indistinguishable from full
// bleed, and it keeps circles round: a 1.25/1.2 non-uniform stretch would turn
// every gauge and the gear icon into ellipses.
#if defined(BOARD_IS_M5TAB5)
constexpr int W = 1280, H = 720;
constexpr int UI_MUL = 6, UI_DIV = 5;    // 1.2x
constexpr int UI_OFFSET_X = 26;          // (1280 - 1228) / 2
#else
constexpr int W = 1024, H = 600;
constexpr int UI_MUL = 1, UI_DIV = 1;
constexpr int UI_OFFSET_X = 0;
#endif
constexpr int BASE_W = 1024, BASE_H = 600;

// Authored (canvas) coordinates -> panel pixels.
inline int slen(int v) {
  return v >= 0 ? (v * UI_MUL + UI_DIV / 2) / UI_DIV
                : -((-v * UI_MUL + UI_DIV / 2) / UI_DIV);
}
inline int sx(int v) { return UI_OFFSET_X + slen(v); }
inline int sy(int v) { return slen(v); }
// Panel pixels -> authored coordinates, for hit testing.
inline int ux(int px) {
  const int v = px - UI_OFFSET_X;
  return v >= 0 ? (v * UI_DIV + UI_MUL / 2) / UI_MUL
                : -((-v * UI_DIV + UI_MUL / 2) / UI_MUL);
}
inline int uy(int py) {
  return py >= 0 ? (py * UI_DIV + UI_MUL / 2) / UI_MUL
                 : -((-py * UI_DIV + UI_MUL / 2) / UI_MUL);
}
constexpr int LEFT = 22, TOP = 76, PANEL_W = 326, PANEL_H = 510;
constexpr int CONTENT = 378, RIGHT = 1002, AMS_Y = 407;
constexpr int PROGRESS_Y = 266;
constexpr int TAB_Y = 86, TAB_H = 50;
constexpr int INFO_X = 924, SETTINGS_X = 980;
constexpr int DETAILS_X = 898, DETAILS_Y = 12, DETAILS_W = 52, DETAILS_H = 52;
constexpr int IMAGE_X = LEFT + 16, IMAGE_Y = 90, IMAGE_W = PANEL_W - 32;
constexpr int IMAGE_CONTENT_H = 236;
constexpr int CAMERA_H = IMAGE_W * 9 / 16;
constexpr int CAMERA_OFFSET_Y = (IMAGE_CONTENT_H - CAMERA_H) / 2;
constexpr int IMAGE_META_Y = IMAGE_Y + CAMERA_OFFSET_Y + CAMERA_H + 16;
constexpr int IMAGE_DOTS_Y = 350;
constexpr int IMAGE_H = IMAGE_DOTS_Y + 12 - IMAGE_Y;
constexpr int PANEL_NETWORK_Y = 392, PANEL_ROW_STEP = 42;
struct Point { int x, y; };
inline bool imageContains(Point p) {
  return p.x >= IMAGE_X && p.x < IMAGE_X + IMAGE_W &&
         p.y >= IMAGE_Y && p.y < IMAGE_Y + IMAGE_H;
}
inline bool homeAmsContains(Point p) {
  // Authored bounds, not the panel size: hit points are un-scaled by ux/uy.
  return p.x >= CONTENT && p.x < RIGHT && p.y >= AMS_Y && p.y < BASE_H;
}
// Verified on the 7B: GT911 coordinates already match the mounted display at
// user rotation 0 (e.g. a gear tap reports x=989, y=42). Only the optional
// user-requested 180-degree rotation needs a coordinate flip.
//
// The Tab5 is different in a way that matters: its GT911 reports in the panel's
// NATIVE PORTRAIT frame (720x1280), because that is how M5GFX configures the
// controller (x_max=719, y_max=1279). The dashboard is nevertheless drawn in
// landscape - display_ui.cpp's sanitizeRotation() maps logical 0/180 onto
// hardware rotation 3/1, and LovyanGFX only swaps the axes for odd rotations.
// So the raw pair has to be swapped and mirrored exactly the way
// Panel_Device::convertRawXY() does it, otherwise every tap lands in the wrong
// place. Result is in panel (1280x720) coordinates; the 1024x600 canvas is
// anchored at the top-left, so points outside it simply miss every widget.
#if defined(BOARD_IS_M5TAB5)
constexpr int RAW_W = 720, RAW_H = 1280;
inline Point touchPoint(int x, int y, uint8_t rotation) {
  const Point panel = (rotation & 2) ? Point{y, RAW_W - 1 - x}   // hardware rotation 1
                                     : Point{RAW_H - 1 - y, x};  // hardware rotation 3
  // Hit testing compares against authored coordinates, so undo the 1.2x the
  // draw boundary applies (ux/uy are identity on the 7B).
  return Point{ux(panel.x), uy(panel.y)};
}
#else
inline Point touchPoint(int x, int y, uint8_t rotation) {
  return (rotation & 2) ? Point{W - 1 - x, H - 1 - y} : Point{x, y};
}
#endif
uint16_t p4ProgressPixel(int x, int width, uint32_t phase, uint16_t accent);
enum class Gesture { None, Tap, Left, Right, Up, Down };
inline Gesture classify(Point start, Point end, uint32_t duration, int excursion) {
  const int dx = end.x - start.x, dy = end.y - start.y;
  const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  if (duration > 1500) return Gesture::None;
  if (ax >= 64 && ax > ay * 2) return dx < 0 ? Gesture::Left : Gesture::Right;
  if (ay >= 64 && ay > ax * 2) return dy < 0 ? Gesture::Up : Gesture::Down;
  // A tap is a press that stays roughly put. 24 authored px is only ~2.9 mm of
  // glass once the Tab5's 1.2x scale is applied, and a fingertip wobbles more
  // than that by itself, so short taps on the header icons were being thrown
  // away as "not a gesture" - the tap simply never reached the hit test. 46 px
  // is ~5.5 mm and still sits well below the 64 px a deliberate swipe needs on
  // its dominant axis, so swipes keep classifying as swipes.
  return excursion <= 46 && duration >= 30 && duration <= 800 ? Gesture::Tap : Gesture::None;
}
// A wake gesture, bus failure, multitouch or modal transition must never turn
// its eventual release into a navigation tap on a newly visible screen.
class TouchTracker {
  bool wasDown = false, tracking = false;
  Point start = {}, end = {};
  uint32_t began = 0;
  int excursion = 0;
public:
  Point origin() const { return start; }
  Gesture update(bool down, bool valid, Point p, uint32_t now, bool enabled) {
    Gesture result = Gesture::None;
    if (!enabled || !valid) tracking = false;
    if (down && !wasDown && enabled && valid) {
      start = end = p; began = now; excursion = 0; tracking = true;
    }
    if (tracking && down) {
      end = p;
      int dx = p.x-start.x, dy = p.y-start.y;
      int distance = (dx<0?-dx:dx)+(dy<0?-dy:dy);
      if (distance>excursion) excursion=distance;
    }
    if (!down && wasDown && tracking) {
      result = classify(start,end,now-began,excursion); tracking=false;
    }
    wasDown=down;
    return result;
  }
};
}
