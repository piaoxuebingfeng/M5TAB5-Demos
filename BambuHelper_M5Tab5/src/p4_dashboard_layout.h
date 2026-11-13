#pragma once
#include <stdint.h>

// Native 7B canvas. Shared by drawing and hit testing, including host previews.
namespace p4ui {
constexpr uint16_t PANEL_BACKGROUND = 0xef7e;  // #eceff1 in RGB565.
constexpr int W = 1024, H = 600;
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
  return p.x >= CONTENT && p.x < RIGHT && p.y >= AMS_Y && p.y < H;
}
// Verified on the 7B: GT911 coordinates already match the mounted display at
// user rotation 0 (e.g. a gear tap reports x=989, y=42). Only the optional
// user-requested 180-degree rotation needs a coordinate flip.
inline Point touchPoint(int x, int y, uint8_t rotation) {
  return (rotation & 2) ? Point{W - 1 - x, H - 1 - y} : Point{x, y};
}
uint16_t p4ProgressPixel(int x, int width, uint32_t phase, uint16_t accent);
enum class Gesture { None, Tap, Left, Right, Up, Down };
inline Gesture classify(Point start, Point end, uint32_t duration, int excursion) {
  const int dx = end.x - start.x, dy = end.y - start.y;
  const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  if (duration > 1500) return Gesture::None;
  if (ax >= 64 && ax > ay * 2) return dx < 0 ? Gesture::Left : Gesture::Right;
  if (ay >= 64 && ay > ax * 2) return dy < 0 ? Gesture::Up : Gesture::Down;
  return excursion <= 24 && duration >= 40 && duration <= 700 ? Gesture::Tap : Gesture::None;
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
