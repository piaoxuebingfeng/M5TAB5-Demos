// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

// Native P4 rendering of the owner's approved BambuDroid composition.
// Assets and spool provenance: assets/p4/NOTICE.md.
#include "display_p4.h"
#if defined(BOARD_IS_WS_P4_7B)
#include "fonts.h"
#include "hms_lookup.h"
#include "settings.h"    // dispSettings.hmsSeverityAll — the alert-visibility bar
#include "p4_printer_artwork.h"
#include "p4_camera_service.h"
#include <lgfx/utility/lgfx_qrcode.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace {
using namespace p4ui;
constexpr uint16_t rgb(unsigned c) { return ((c >> 8) & 0xf800) | ((c >> 5) & 0x7e0) | ((c >> 3) & 0x1f); }
constexpr uint16_t BG = rgb(0x111416), FG = rgb(0xf3f7f5), MUTED = rgb(0xbec7ce);
constexpr uint16_t GREEN = rgb(0x65d9a8), LINE = rgb(0x3c464e), SURFACE = rgb(0x1e2327);
constexpr uint16_t PAPER = PANEL_BACKGROUND, INK = rgb(0x171b1e), DIM = rgb(0x46515a);
constexpr uint16_t ORANGE = rgb(0xffc17e), RED = rgb(0xf08080);
struct Model { const char* prefix; const char* name; const uint8_t* png; size_t bytes; bool chamber; bool fans; };
#define MODEL(prefix, name, asset, chamber, fans) {prefix, name, p4_art_##asset, sizeof(p4_art_##asset), chamber, fans}
const Model models[] = {
  MODEL("00M", "X1 Carbon", x1c, true, true), MODEL("00W", "X1", x1, true, true),
  MODEL("03W", "X1E", x1e, true, true), MODEL("01S", "P1P", p1p, false, true),
  MODEL("01P", "P1S", p1s, false, true), MODEL("22E", "P2S", p2s, true, true),
  MODEL("20P", "X2D", x2d, true, true), MODEL("030", "A1 mini", a1mini, false, false),
  MODEL("039", "A1", a1, false, false), MODEL("26A", "A2L", a1, false, false),
  MODEL("094", "H2D", h2d, true, true), MODEL("239", "H2D Pro", h2dpro, true, true),
  MODEL("31B", "H2C", h2c, true, true), MODEL("093", "H2S", h2s, true, true)
};
const Model unknown = {"", "Bambu Lab", nullptr, 0, false, false};
const Model& modelFor(const PrinterConfig& config) {
  for (const auto& m : models) if (strncmp(config.serial, m.prefix, 3) == 0) return m;
  return unknown;
}

// Every coordinate in this file was authored against the 7B's 1024x600 canvas.
// The Tab5 panel is 1280x720, so the composition is scaled once here - at the
// only place pixels reach the panel - instead of re-authoring ~200 hand-tuned
// positions. p4ui::sx/sy/slen do the mapping; p4ui::ux/uy undo it for hit
// testing, so a tap still lands on the widget it visually hits. On the 7B the
// transform is the identity and the generated code is unchanged.
//
// Text sizes are deliberately NOT scaled: a VLW face resampled to 1.2x picks up
// uneven nearest-neighbour steps, and the Tab5's 294ppi panel keeps a native
// face perfectly readable. Only glyph *positions* scale.
class ScaledGfx {
public:
  lgfx::LovyanGFX& g;
  explicit ScaledGfx(lgfx::LovyanGFX& target) : g(target) {}
  lgfx::LovyanGFX& raw() { return g; }

  // Colours MUST stay uint16_t. LovyanGFX dispatches on the argument type:
  // a uint16_t selects the native RGB565 path, while a uint32_t selects the
  // RGB888 one and converts a second time - 0x10A2 (the dark background)
  // comes out as pure blue if it is widened. Keep every signature exact.
  void fillScreen(uint16_t c) { g.fillScreen(c); }
  void fillRect(int x, int y, int w, int h, uint16_t c) {
    g.fillRect(p4ui::sx(x), p4ui::sy(y), p4ui::slen(w), p4ui::slen(h), c);
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    g.fillRoundRect(p4ui::sx(x), p4ui::sy(y), p4ui::slen(w), p4ui::slen(h), p4ui::slen(r), c);
  }
  void drawRect(int x, int y, int w, int h, uint16_t c) {
    g.drawRect(p4ui::sx(x), p4ui::sy(y), p4ui::slen(w), p4ui::slen(h), c);
  }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    g.drawRoundRect(p4ui::sx(x), p4ui::sy(y), p4ui::slen(w), p4ui::slen(h), p4ui::slen(r), c);
  }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
    g.drawLine(p4ui::sx(x0), p4ui::sy(y0), p4ui::sx(x1), p4ui::sy(y1), c);
  }
  void drawCircle(int x, int y, int r, uint16_t c) {
    g.drawCircle(p4ui::sx(x), p4ui::sy(y), p4ui::slen(r), c);
  }
  void fillCircle(int x, int y, int r, uint16_t c) {
    g.fillCircle(p4ui::sx(x), p4ui::sy(y), p4ui::slen(r), c);
  }
  void drawFastVLine(int x, int y, int h, uint16_t c) {
    g.drawFastVLine(p4ui::sx(x), p4ui::sy(y), p4ui::slen(h), c);
  }
  void startWrite() { g.startWrite(); }
  void endWrite() { g.endWrite(); }
  void setClipRect(int x, int y, int w, int h) {
    g.setClipRect(p4ui::sx(x), p4ui::sy(y), p4ui::slen(w), p4ui::slen(h));
  }
  void clearClipRect() { g.clearClipRect(); }
  void setFont(const lgfx::IFont* f) { g.setFont(f); }
  void setTextSize(float s) { g.setTextSize(s); }
  void setTextDatum(lgfx::textdatum_t d) { g.setTextDatum(d); }
  void setTextColor(uint16_t fg, uint16_t bg) { g.setTextColor(fg, bg); }
  void setTextWrap(bool w) { g.setTextWrap(w); }
  void drawString(const char* s, int x, int y) { g.drawString(s, p4ui::sx(x), p4ui::sy(y)); }
  // Authored width, so callers can keep subtracting it from authored anchors.
  // uy(), not ux(): ux() maps an absolute panel coordinate and therefore
  // subtracts UI_OFFSET_X, which made every measured width ~22 authored px too
  // narrow on the Tab5 - that is what squeezed the "%" of "44%" into the digit.
  int textWidth(const char* s) { return p4ui::uy(g.textWidth(s)); }
};

// Select parsed fonts directly; never invalidate display_ui's target-agnostic
// setFont cache. The caller resets that cache before returning to legacy views.
class Canvas {
public:
  ScaledGfx& g;
  explicit Canvas(ScaledGfx& target) : g(target) {}
  int text(const char* value, int x, int y, int width, uint16_t color = FG,
           FontID face = FONT_SMALL_2X, float scale = 1, uint16_t background = BG,
           bool middle = false, bool centered = false) {
    if (width <= 0) return 0;
    const bool cjk = needsCjkFont(value);
    const bool fixedFace = face == FONT_CJK || face == FONT_P4_CN_SMALL || face == FONT_P4_DURATION;
    const bool smallCjk = cjk && !fixedFace && scale < .91f;
    g.setTextSize(smallCjk ? 1 : scale);
    const lgfx::IFont* f = cachedFont(cjk && !fixedFace ? (smallCjk ? FONT_P4_CN_SMALL : FONT_CJK) : face);
    g.setFont(f ? f : &lgfx::fonts::Font2);
    g.setTextDatum(centered ? lgfx::textdatum_t::middle_center :
      middle ? lgfx::textdatum_t::middle_left : lgfx::textdatum_t::top_left);
    g.setTextColor(color, background);
    char fitted[256]; snprintf(fitted, sizeof(fitted), "%s", value);
    // Trim on UTF-8 boundaries; long filenames must never bleed into actions.
    bool clipped = false;
    while (fitted[0] && g.textWidth(fitted) > width - (clipped ? g.textWidth("...") : 0)) {
      size_t n = strlen(fitted) - 1;
      while (n && (static_cast<unsigned char>(fitted[n]) & 0xc0) == 0x80) --n;
      fitted[n] = 0; clipped = true;
    }
    if (clipped) strlcat(fitted, "...", sizeof(fitted));
    g.drawString(fitted, x, y);
    const int result = g.textWidth(fitted);
    g.setTextSize(1);
    return result;
  }
  int textRight(const char* value, int x, int y, int width, uint16_t color,
                FontID face, float scale = 1, uint16_t background = BG) {
    // The title status has a fixed, bounded width, so measure it in one pass.
    g.setTextSize(scale);
    const lgfx::IFont* f = cachedFont(face);
    g.setFont(f ? f : &lgfx::fonts::Font2);
    g.setTextDatum(lgfx::textdatum_t::middle_right);
    g.setTextColor(color, background);
    g.drawString(value, x + width, y);
    const int result = g.textWidth(value);
    g.setTextSize(1);
    return result;
  }
  // Box height of a face at a given scale, in authored units. middle-anchored
  // text centres this whole box, so a caller that anchors at the middle needs
  // it back to find the baseline underneath.
  int boxHeight(FontID face, float scale) {
    g.setTextSize(scale);
    const lgfx::IFont* f = cachedFont(face);
    g.setFont(f ? f : &lgfx::fonts::Font2);
    const int h = p4ui::uy(g.raw().fontHeight());
    g.setTextSize(1);
    return h;
  }
  // Baseline-anchored draw. A small suffix must sit on the big number's
  // baseline; hanging it off its own descender is what put the "%" of "44%"
  // into the corner of the digit.
  int textOnBaseline(const char* value, int x, int baseline, int width,
                     uint16_t color, FontID face, float scale,
                     uint16_t background = BG) {
    if (width <= 0) return 0;
    g.setTextSize(scale);
    const lgfx::IFont* f = cachedFont(face);
    g.setFont(f ? f : &lgfx::fonts::Font2);
    g.setTextDatum(lgfx::textdatum_t::baseline_left);
    g.setTextColor(color, background);
    g.drawString(value, x, baseline);
    const int result = g.textWidth(value);
    g.setTextSize(1);
    return result;
  }
  void pill(const char* value, int x, int y, int w, uint16_t color = GREEN) {
    g.fillRoundRect(x, y, w, 38, 10, SURFACE);
    text(value, x + 12, y + 7, w - 24, color, FONT_SMALL_2X, .88f, SURFACE);
  }
};
const char* stateName(const BambuState& s, bool networkOnline) {
  if (!networkOnline) return "网络重连中";
  if (!s.connected) return "打印机重连中";
  switch (s.gcodeStateId) {
    case GCODE_RUNNING: return "正在打印";
    case GCODE_PAUSE: return "打印已暂停";
    case GCODE_PREPARE: return "准备打印";
    case GCODE_FINISH: return "打印完成";
    case GCODE_FAILED: return "打印已停止";
    case GCODE_IDLE: return "已就绪";
    default: return "等待打印机数据";
  }
}
uint16_t accent(const BambuState& s) {
  if (!s.connected) return MUTED;
  return s.gcodeStateId == GCODE_FAILED ? RED : s.gcodeStateId == GCODE_PAUSE ? ORANGE : GREEN;
}
} // namespace

uint16_t accentP4(const BambuState& s) { return accent(s); }
uint16_t p4ui::p4ProgressPixel(int x, int width, uint32_t phase, uint16_t accent) {
  const uint32_t denom = width > 1 ? width - 1 : 1;
  const uint8_t gradient = 45 + (width > 0 ? x * 75 / denom : 0);
  auto channel = [&](uint16_t mask, uint8_t shift, int bright) {
    const int base = (accent & mask) >> shift;
    return static_cast<uint16_t>((base + (bright - base) * gradient / 255) << shift);
  };
  uint16_t color = channel(0xf800, 11, 23) | channel(0x07e0, 5, 58) | channel(0x001f, 0, 25);
  if (width < 20) return color;
  const int center = static_cast<int>(phase % (width + 160)) - 80;
  const int distance = std::abs(x - center);
  constexpr int half = 56;
  if (distance >= half) return color;
  const int light = (half - distance) * 235 / half;
  auto shine = [&](uint16_t mask, uint8_t shift, int bright) {
    const int base = (color & mask) >> shift;
    return static_cast<uint16_t>((base + (bright - base) * light / 255) << shift);
  };
  return shine(0xf800, 11, 31) | shine(0x07e0, 5, 63) | shine(0x001f, 0, 31);
}
void drawP4Progress(lgfx::LovyanGFX& g, int x, int y, int width,
                    uint8_t percent, uint32_t phase, uint16_t accent) {
  // x/y/width are PANEL pixels (this function also serves the partial-refresh
  // path in display_p4_runtime.cpp). The strip's own thickness follows the
  // dashboard scale so it keeps its authored 8px weight on the 7B and 10px on
  // the Tab5.
  const int h = p4ui::slen(8), r = p4ui::slen(4), cap = p4ui::slen(4);
  g.startWrite();
  g.setClipRect(x, y, width, h);
  g.fillRoundRect(x, y, width, h, r, LINE);
  const int filled = width * std::min<unsigned>(100, percent) / 100;
  if (filled) {
    g.fillRoundRect(x, y, filled, h, r, accent);
    const int from = std::min(cap, filled), to = std::max(from, filled - cap);
    for (int px = from; px < to; ++px)
      g.drawFastVLine(x + px, y, h, p4ui::p4ProgressPixel(px, filled, phase, accent));
  }
  g.clearClipRect();
  g.endWrite();
}

namespace {
bool known(const BambuState& s, uint32_t flag) { return (s.p4Known & flag) != 0; }
// Same rule bambu_mqtt.cpp applies for "this slot can actually connect": a
// cloud slot needs a serial plus the account id the worker fills in, a LAN slot
// needs a serial plus an address. Used only to decide whether the header advertises
// a switch, never to gate any traffic.
bool slotReady(uint8_t slot) {
  if (slot >= MAX_ACTIVE_PRINTERS) return false;
  const PrinterConfig& cfg = printers[slot].config;
  return isCloudMode(cfg.mode) ? (cfg.serial[0] && cfg.cloudUserId[0])
                               : (cfg.serial[0] && cfg.ip[0]);
}
void duration(char* out, size_t n, uint16_t minutes) {
  if (minutes >= 60) snprintf(out, n, "%u小时 %02u分", minutes / 60, minutes % 60);
  else snprintf(out, n, "%u分钟", minutes);
}
const char* speedName(uint8_t speed) {
  switch (speed) {
    case 1: return "静音 50%"; case 2: return "标准 100%";
    case 3: return "运动 124%"; case 4: return "狂暴 166%";
    default: return "—";
  }
}
const char* wifiQuality(int rssi) {
  if (rssi >= 0) return "—";
  return rssi >= -50 ? "Wi-Fi 极佳" : rssi >= -65 ? "Wi-Fi 良好" : rssi >= -75 ? "Wi-Fi 一般" : "Wi-Fi 较弱";
}

void header(Canvas& cv, const PrinterSlot& p, const P4DashboardView& v, time_t now, const char* deviceIp) {
  char name[100];
  // A name the user typed as the model name ("P1S") would otherwise render as
  // "P1S · P1S"; keep the model only when it adds information.
  const char* model = modelFor(p.config).name;
  if (p.config.name[0] && model && strcasecmp(p.config.name, model) == 0)
    snprintf(name, sizeof(name), "%s", p.config.name);
  else
    snprintf(name, sizeof(name), "%s · %s", p.config.name[0] ? p.config.name : "打印机", model);
  constexpr int headerAxis = 38;
  int end = LEFT + cv.text(name, LEFT, headerAxis, 320, FG, FONT_CJK, 1, BG, true);
  // The name row doubles as the multi-printer switch (see p4DashboardGesture:
  // a tap left of x=790 on the home page asks for the next configured slot).
  // Draw the caret only when a second slot is actually ready, so a
  // single-printer setup keeps the original header pixel-for-pixel.
  uint8_t readySlots = 0;
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i) if (slotReady(i)) ++readySlots;
  if (readySlots > 1) {
    // +20 rather than +10: Canvas::text() reports textWidth() for the fitted
    // string, and the CJK fallback face advances slightly wider than it
    // measures, so a 10px gap drew the caret on top of the last glyph.
    const int cx = end + 20, cy = headerAxis;
    cv.g.drawLine(cx, cy - 4, cx + 5, cy + 2, FG);
    cv.g.drawLine(cx + 5, cy + 2, cx + 10, cy - 4, FG);
    end = cx + 16;
  }
  if (deviceIp && deviceIp[0]) cv.text(deviceIp, end + 28, headerAxis, std::max(0, 784 - end - 28), MUTED, FONT_CJK, 1, BG, true);
  char clock[16] = "--:--";
  if (now > 1700000000) { struct tm local; localtime_r(&now, &local); strftime(clock, sizeof(clock), "%H:%M", &local); }
  cv.text(clock, 814, 22, 86, FG, FONT_BODY_2X, .85f);
  // Draw icons with primitives so they remain crisp at the native resolution.
  if (v.page >= 0) {
    cv.g.drawLine(928, 27, 914, 38, FG); cv.g.drawLine(914, 38, 928, 49, FG);
    cv.g.drawLine(914, 38, 941, 38, FG);
  } else {
    cv.g.drawCircle(INFO_X,38,12,FG);
    cv.g.fillCircle(INFO_X,31,1,FG);
    cv.g.fillRect(INFO_X-1,36,3,10,FG);
#if HAS_HMS_UI
    // Alert badge: a dot on the info icon, never text on the print page.
    // Same bar the Bambu app clears (errorBadgeFor): connection-time
    // baselines, self-resolved events and severity-3 advisories never light
    // it — only what a phone would actually notify about.
    if (const ErrorBadge badge = errorBadgeFor(p.state); badge.active) {
      cv.g.fillCircle(INFO_X+10,27,6,BG);        // ring gap against the icon stroke
      cv.g.fillCircle(INFO_X+10,27,5,badge.severity <= 1 ? RED : ORANGE);
    }
#endif
  }
  // A continuous toothed outline shares the info button's 24px envelope and
  // one-pixel stroke. No filled disk or independent outer circle.
  const auto gearPoint = [](float angle, float radius, int& x, int& y) {
    x = SETTINGS_X + lroundf(cosf(angle) * radius);
    y = 38 + lroundf(sinf(angle) * radius);
  };
  int firstX=0,firstY=0,previousX=0,previousY=0;
  for (int i = 0; i < 8; ++i) {
    constexpr float step = 3.14159265f / 4;
    for (int corner = 0; corner < 4; ++corner) {
      const float a = i * step + (corner < 2 ? -.20f : .20f) * step;
      const float radius = corner == 0 || corner == 3 ? 9 : 12;
      int x=0,y=0; gearPoint(a,radius,x,y);
      if (!i && !corner) { firstX=x; firstY=y; }
      else cv.g.drawLine(previousX,previousY,x,y,FG);
      previousX=x; previousY=y;
    }
  }
  cv.g.drawLine(previousX,previousY,firstX,firstY,FG);
  cv.g.drawCircle(SETTINGS_X,38,4,FG);
}

void printerPanel(Canvas& cv, const PrinterSlot& p, const P4DashboardView& view, time_t now) {
  const auto& s = p.state;
  const auto& model = modelFor(p.config);
  cv.g.fillRoundRect(LEFT, TOP, PANEL_W, PANEL_H, 20, PAPER);
  // Decode only when the printer model changes; the thumbnail is static.
  static lgfx::LGFX_Sprite art;
  static const Model* artModel = nullptr;
  // The thumbnail sprite lives in PANEL pixels, so its size - and the fallback
  // drawing inside it - follows the dashboard scale rather than the authored
  // 1024x600 geometry.
  const int artW = p4ui::slen(IMAGE_W), artH = p4ui::slen(IMAGE_CONTENT_H);
  if (artModel != &model || !art.getBuffer()) {
    if (!art.getBuffer()) { art.setPsram(true); art.setColorDepth(16); art.createSprite(artW, artH); }
    if (art.getBuffer()) {
      art.fillSprite(PAPER);
      if (model.png) art.drawPng(model.png, model.bytes, 0, 0, artW, artH, 0, 0, 0, 0, lgfx::datum_t::middle_center);
      else {
        art.drawRoundRect(p4ui::slen(82), p4ui::slen(8), p4ui::slen(96), p4ui::slen(130), p4ui::slen(7), DIM);
        art.drawRect(p4ui::slen(92), p4ui::slen(30), p4ui::slen(76), p4ui::slen(96), DIM);
      }
      artModel = &model;
    }
  }
  bool showPrinter=true;
#if defined(P4_CLOUD_CAMERA_EXPERIMENTAL)
  if(view.showCamera){
    showPrinter=false;
    if(!drawP4Camera(cv.g.raw(),p,p4ui::sx(IMAGE_X),p4ui::sy(IMAGE_Y))) {
      cv.text(p4CameraBusy()?"正在获取实景":"实景暂未就绪",LEFT+PANEL_W/2,
              IMAGE_Y+IMAGE_CONTENT_H/2,IMAGE_W,DIM,FONT_P4_CN_SMALL,1,PAPER,true,true);
    }
    P4CameraTiming timing;
    p4CameraTiming(p,timing);
    auto clockText=[](time_t stamp,char* out,size_t capacity) {
      tm local{};
      if (stamp && localtime_r(&stamp,&local)) strftime(out,capacity,"%H:%M:%S",&local);
      else snprintf(out,capacity,"暂无");
    };
    char captured[32],stamp[32],line[112];
    clockText(timing.capturedAt,captured,sizeof(captured));
    const char* next=nullptr;
    if(timing.busy) next="正在更新";
    else if(!timing.automatic) next="已关闭";
    else if(!strcmp(timing.blockedBy,"network")) next="等待网络";
    else if(!strcmp(timing.blockedBy,"mqtt")) next="等待打印机";
    else if(timing.blockedBy[0]) next="等待就绪";
    else if(!timing.nextUpdateAt || timing.nextUpdateAt<=now) next="即将更新";
    if(!next) {clockText(timing.nextUpdateAt,stamp,sizeof(stamp));next=stamp;}
    snprintf(line,sizeof(line),"获取 %s  下次 %s",captured,next);
    cv.text(line,LEFT+PANEL_W/2-10,IMAGE_META_Y,IMAGE_W,DIM,FONT_P4_CN_SMALL,1,PAPER,true,true);
  }
#endif
  if(showPrinter&&art.getBuffer())art.pushSprite(&cv.g.raw(),p4ui::sx(IMAGE_X),p4ui::sy(IMAGE_Y));
  // Contain both sources in a fixed viewport. Neither switching nor the
  // source aspect ratio can move the dots or any of the information rows.
  constexpr int dotsY=IMAGE_DOTS_Y;
  cv.g.fillCircle(LEFT+PANEL_W/2-8,dotsY,4,view.showCamera?rgb(0xb8c2c8):DIM);
  cv.g.fillCircle(LEFT+PANEL_W/2+8,dotsY,4,view.showCamera?DIM:rgb(0xb8c2c8));
  // All product metrics use one 20px face and shared row axes. In particular,
  // Latin values never silently select a larger Inter face than Chinese ones.
  constexpr int networkY=PANEL_NETWORK_Y,speedY=networkY+PANEL_ROW_STEP,
                nozzleY=speedY+PANEL_ROW_STEP,fanY=nozzleY+PANEL_ROW_STEP,fanLabelY=fanY+32;
  auto productText = [&](const char* value,int x,int y,int width,uint16_t color) {
    cv.text(value,x,y,width,color,FONT_P4_CN_SMALL,1,PAPER,true);
  };
  productText("网络",42,networkY,68,DIM);
  productText(wifiQuality(s.wifiSignal),110,networkY,IMAGE_X+IMAGE_W-110,INK);
  productText("速度",42,speedY,68,DIM);
  productText(speedName(s.speedLevel),110,speedY,IMAGE_X+IMAGE_W-110,INK);
  productText("喷嘴",42,nozzleY,68,DIM);
  char nozzle[24] = "—";
  const unsigned nozzleId = s.activeNozzle > 1 ? 0 : s.activeNozzle;
  const float diameter = s.nozzleDiameterN[nozzleId] > 0 ? s.nozzleDiameterN[nozzleId] : s.nozzleDiameter;
  if (diameter > 0) snprintf(nozzle, sizeof(nozzle), "%.1f mm", diameter);
  const char* rawType = s.nozzleTypeN[nozzleId][0] ? s.nozzleTypeN[nozzleId] : s.nozzleType;
  const char* type = strcmp(rawType, "hardened_steel") == 0 ? "硬化钢" : strcmp(rawType, "stainless_steel") == 0 ? "不锈钢" : rawType;
  if (strlen(rawType) >= 4 && rawType == type) {
    if (rawType[2]=='0' && rawType[3]=='0') type="不锈钢";
    else if (rawType[2]=='0' && rawType[3]=='1') type="硬化钢";
    else if (rawType[2]=='0' && rawType[3]=='5') type="碳化钨";
  }
  char nozzleLine[96];
  snprintf(nozzleLine, sizeof(nozzleLine), "%s  %s", nozzle, type[0] ? type : "规格未上报");
  productText(nozzleLine,110,nozzleY,IMAGE_X+IMAGE_W-110,INK);
  productText("风扇",42,fanY,68,DIM);
  const uint8_t values[] = {s.coolingFanPct,s.auxFanPct,s.chamberFanPct};
  const uint32_t flags[] = {P4_PART_FAN,P4_AUX_FAN,P4_CHAMBER_FAN};
  const char* labels[] = {"部件","辅助","腔体"};
  for (int i=0;i<(model.fans?3:1);++i) {
    char b[16];
    if (known(s,flags[i])) snprintf(b,sizeof(b),"%u%%",values[i]); else snprintf(b,sizeof(b),"—");
    productText(b,110+i*65,fanY,65,INK);
    productText(labels[i],110+i*65,fanLabelY,65,DIM);
  }
}

void temperature(Canvas& cv, const char* name, float value, float target, bool hasValue,
                 bool hasTarget, int x, int width, bool active) {
  cv.text(name,x,301,width,active?GREEN:MUTED,FONT_P4_CN_SMALL,1,BG,true);
  char b[48]; if (hasValue) snprintf(b, sizeof(b), "%.0f°", value); else snprintf(b, sizeof(b), "—");
  cv.text(b,x-1,345,width,FG,FONT_P4_VALUE,.85f,BG,true);
  if (!hasTarget) snprintf(b, sizeof(b), "实时温度");
  else if (target <= 0) snprintf(b, sizeof(b), "未加热");
  else snprintf(b, sizeof(b), "目标 %.0f°", target);
  cv.text(b,x,375,width,MUTED,FONT_P4_CN_SMALL,1,BG,true);
}
void job(Canvas& cv, const PrinterSlot& p, time_t now, bool networkOnline, uint32_t progressPhase) {
  const auto& s = p.state;
  char taskName[128]={};
#if defined(P4_CLOUD_CAMERA_EXPERIMENTAL)
  p4CameraJobName(p,taskName,sizeof(taskName));
#endif
  const char* stage = runningStageLabel(s);
  const char* status = !s.connected ? stateName(s,networkOnline) :
                       stage ? stage : s.gcodeStateId != GCODE_RUNNING ? stateName(s,networkOnline) : nullptr;
  int titleWidth = RIGHT - CONTENT;
  if (status) {
    constexpr int statusWidth = 200;
    const int used = cv.textRight(status, RIGHT - statusWidth, 96, statusWidth,
                                  accent(s), FONT_P4_CN_SMALL);
    titleWidth = std::max(0, RIGHT - used - 20 - CONTENT);
  }
  cv.text(taskName[0]?taskName:s.subtaskName[0] ? jobDisplayName(s) : "等待新的打印任务",
          CONTENT,96,titleWidth,FG,FONT_CJK,1,BG,true);
#if HAS_HMS_UI
  // No inline alert text on the print page: the header's info-icon dot is the
  // only main-page alert surface (see header()); codes and counts live in the
  // details page behind it.
#endif
  char b[64];
  if (known(s, P4_PROGRESS)) snprintf(b, sizeof(b), "%u", std::min<unsigned>(100, s.progress)); else snprintf(b, sizeof(b), "—");
  // The number is middle-anchored, the "%" hangs off its baseline. The two
  // faces are ~76 px and ~24 px tall here, so the suffix used to be placed
  // from the advance width alone and landed 2 px from the digit's stem; PCT_GAP
  // is the authored air between them, and the baseline is measured instead of
  // guessed so the pair stays aligned on the 7B's 1:1 canvas as well.
  constexpr int PCT_Y = 176, PCT_GAP = 6;
  // Every P4 face is an Inter subset, so the ascent/descent split is the same
  // across them (109/28 at the progress face): 28/137 of the box sits below the
  // baseline. That converts a middle-anchored box into the baseline the suffix
  // has to hang from - ink bottom, not box bottom, which is 13 px lower here.
  constexpr float BASELINE_FROM_MIDDLE = 0.2956f;
  const int width = cv.text(b,CONTENT-3,PCT_Y,226,FG,FONT_P4_PROGRESS,.95f,BG,true);
  if (known(s, P4_PROGRESS))
    cv.textOnBaseline("%",CONTENT-3+width+PCT_GAP,
                      PCT_Y + (int)(cv.boxHeight(FONT_P4_PROGRESS,.95f) * BASELINE_FROM_MIDDLE),
                      45,accent(s),FONT_BODY_2X,1.15f);
  cv.text("打印进度",CONTENT,238,190,MUTED,FONT_P4_CN_SMALL,1,BG,true);
  cv.text("预计剩余",608,142,220,MUTED,FONT_P4_CN_SMALL,1,BG,true);
  if (known(s, P4_REMAINING) && s.printing) duration(b, sizeof(b), s.remainingMinutes); else snprintf(b, sizeof(b), "—");
  cv.text(b,605,188,240,FG,FONT_P4_DURATION,1,BG,true);
  if (s.connected && s.gcodeStateId == GCODE_RUNNING && known(s, P4_REMAINING) && now > 1700000000) {
    time_t finish = now + s.remainingMinutes * 60;
    struct tm local; localtime_r(&finish, &local); char hhmm[8]; strftime(hhmm, sizeof(hhmm), "%H:%M", &local);
    snprintf(b, sizeof(b), "%s 完成", hhmm); cv.text(b,608,234,230,MUTED,FONT_P4_CN_SMALL,1,BG,true);
  }
  cv.text("当前层数",850,142,150,MUTED,FONT_P4_CN_SMALL,1,BG,true);
  if (s.totalLayers) snprintf(b, sizeof(b), "%u", s.layerNum); else snprintf(b, sizeof(b), "—");
  cv.text(b,848,188,154,FG,FONT_P4_VALUE,1,BG,true);
  if (s.totalLayers) { snprintf(b, sizeof(b), "/ %u 层", s.totalLayers); cv.text(b,850,234,152,MUTED,FONT_P4_CN_SMALL,1,BG,true); }
  drawP4Progress(cv.g.raw(),p4ui::sx(CONTENT),p4ui::sy(PROGRESS_Y),p4ui::slen(RIGHT-CONTENT),
                 known(s,P4_PROGRESS)?s.progress:0,progressPhase,accent(s));
  const bool chamber = modelFor(p.config).chamber || known(s,P4_CHAMBER);
  const int count = (s.dualNozzle ? 3 : 2) + (chamber ? 1 : 0), step = (RIGHT-CONTENT)/count;
  if (s.dualNozzle) {
    const unsigned active = s.activeNozzle > 1 ? 0 : s.activeNozzle;
    temperature(cv, active == 1 ? "左喷嘴" : "右喷嘴", s.nozzleTempN[active], s.nozzleTargetN[active], known(s,active ? P4_NOZZLE_L : P4_NOZZLE_R), known(s,active ? P4_NOZZLE_L : P4_NOZZLE_R), CONTENT, step-8, true);
    temperature(cv, active == 1 ? "右喷嘴" : "左喷嘴", s.nozzleTempN[1-active], s.nozzleTargetN[1-active], known(s,active ? P4_NOZZLE_R : P4_NOZZLE_L), known(s,active ? P4_NOZZLE_R : P4_NOZZLE_L), CONTENT+step, step-8, false);
  } else temperature(cv, "喷嘴", s.nozzleTemp, s.nozzleTarget, known(s,P4_NOZZLE), known(s,P4_NOZZLE_TARGET), CONTENT, step-8, true);
  temperature(cv, "热床", s.bedTemp, s.bedTarget, known(s,P4_BED), known(s,P4_BED_TARGET), CONTENT+step*(s.dualNozzle ? 2 : 1), step-8, false);
  if (chamber) temperature(cv, "腔体", s.chamberTemp, 0, known(s,P4_CHAMBER), false, CONTENT+step*(count-1), step-8, false);
}

// Unit pages retain physical ids; the fifth/sixth AMS never aliases a stored
// four-tray unit. Their unavailable tray values are explicitly shown unknown.
const AmsUnit* pageUnit(const AmsState& a, uint8_t page, uint8_t& index) {
  for (uint8_t i = 0; i < a.unitCount && i < AMS_MAX_UNITS; ++i) {
    if (!a.units[i].present) continue;
    if (!page--) { index=i; return &a.units[i]; }
  }
  return nullptr; // external spool, or no AMS
}
void spool(Canvas& cv, const AmsTray* t, bool active, const char* label, int x, int y, int width) {
  const bool loaded = t && t->present;
  cv.g.fillRoundRect(x, y, 41, 69, 6, rgb(0x62676c));
  cv.g.fillRoundRect(x+3, y+3, 35, 63, 4, rgb(0xa3a8ac));
  if (loaded) {
    // Unknown remain still draws a full colour block: the colour is known, it
    // is the measurement that is missing. No "?" on the spool — it reads as
    // "unknown filament", and the Bambu app leaves the block clean too.
    const int h = t->remain < 0 ? 35 : 5 + std::min<int>(100,t->remain)*40/100;
    cv.g.fillRect(x+9, y+56-h, 23, h, t->colorRgb565);
    for (int stripe=11; stripe < 32; stripe+=3) cv.g.drawFastVLine(x+stripe,y+56-h,h,LINE);
  }
  cv.g.fillRect(x+5,y+14,3,44,DIM); cv.g.fillRect(x+33,y+14,3,44,DIM);
  if (active) { cv.g.drawRoundRect(x-1,y-1,43,71,7,GREEN); cv.g.drawRoundRect(x-2,y-2,45,73,8,GREEN); }
  cv.text(label,x+52,y+1,48,active?GREEN:MUTED,FONT_SMALL_2X,.95f);
  cv.text(loaded && t->type[0] ? t->type : t ? "空料槽" : "未上报",x+96,y+1,width-188,FG,FONT_SMALL_2X,.85f);
  char amount[16] = "—";
  if (loaded && t->remain >= 0) snprintf(amount,sizeof(amount),"%d%%",t->remain);
  cv.text(amount,x+width-78,y+1,78,active?GREEN:FG,FONT_BODY_2X,.95f);
  // No nominal 1 kg assumption: the protocol here contains percentage only.
  cv.text(active?"正在使用":loaded?"已装载":t?"未装载":"数据不可用",x+52,y+40,width-136,active?GREEN:MUTED,FONT_SMALL_2X,.8f);
  cv.text("余量",x+width-70,y+41,68,MUTED,FONT_SMALL_2X,.75f);
}
void ams(Canvas& cv, const BambuState& s, uint8_t page, int x, int top, int width, bool detail) {
  const auto& a=s.ams; const uint8_t pages=p4AmsPageCount(a);
  if (!pages) {
    cv.text("连接 AMS 或外挂后显示耗材",x+width/2,top+(detail?155:94),width,
            MUTED,FONT_P4_CN_SMALL,1,BG,true,true);
    return;
  }
  page=page%pages;
  uint8_t index=0; const auto* u=pageUnit(a,page,index);
  char title[48], env[96]="", counter[24];
  if (u) {
    if (u->id >= 128) snprintf(title,sizeof(title),"AMS HT %u",u->id-127);
    else snprintf(title,sizeof(title),"AMS %c",'A'+u->id);
    char rh[20]; formatAmsHumidity(rh,sizeof(rh),*u);
    if (u->temp > 0) snprintf(env,sizeof(env),"%s RH · %.0f°",rh,u->temp);
    else snprintf(env,sizeof(env),"%s RH",rh);
    if (u->dryRemainMin) { size_t len=strlen(env); snprintf(env+len,sizeof(env)-len," · 烘干 %u分",u->dryRemainMin); }
  } else snprintf(title,sizeof(title),"外挂料架");
  // Inter's home title starts 6px below its drawing origin. Lifting the
  // title slightly balances its visible gaps to the temperature and trays.
  const int titleY = detail ? top : top - 3;
  const int end=x+cv.text(title,x,titleY,width-180,FG,FONT_SMALL_2X,1);
  cv.text(env,end+14,titleY+2,std::max(0,x+width-145-end-14),MUTED,FONT_SMALL_2X,.8f);
  snprintf(counter,sizeof(counter),"< %u / %u >",page+1,pages);
  cv.text(counter,x+width-122,titleY,122,pages>1?GREEN:MUTED,FONT_SMALL_2X,.85f);
  const int rowHeight=detail?122:78, colWidth=width/2;
  const int spoolTop = detail ? top + 39 : top + 40;
  unsigned count=u?(u->id>=128?1:4):1;
  for (unsigned j=0;j<count;++j) {
    const AmsTray* t=nullptr; AmsTray external={}; bool active=false; char label[12];
    if (u) {
      if (u->id >= 128) snprintf(label,sizeof(label),"%u",j+1);
      else snprintf(label,sizeof(label),"%c%u",'A'+u->id,j+1);
      if (index < a.trayUnitCount && index < AMS_TRAY_UNITS) {
        t=&a.trays[index*4+j]; active=a.activeTray==index*4+j;
      } else if (a.activeTray==AMS_TRAY_OVERFLOW && a.ovUnitId==u->id && a.ovTrayId==j) { t=&a.ovTray; active=true; }
    } else {
      snprintf(label,sizeof(label),"EXT"); external.present=a.vtPresent;
      external.remain=-1; external.colorRgb565=a.vtColorRgb565;
      snprintf(external.type,sizeof(external.type),"%s",a.vtType); t=&external; active=a.activeTray==254;
    }
    // Keep a visible gap between the home's two tray rows and bottom space
    // matching the white printer panel; the detail page retains its full grid.
    const int rowY = spoolTop + (j/2) * rowHeight - (!detail && j >= 2 ? 5 : 0);
    spool(cv,t,active,label,x+(j%2)*colWidth,rowY,colWidth-14);
  }
  if (detail) cv.text("左右滑动切换 AMS · 未上报的余量保持未知",x,top+310,width,MUTED,FONT_SMALL_2X,.9f);
}
void row(Canvas& cv,const char* label,const char* value,int y,uint16_t color=FG) {
  cv.text(label,48,y,220,MUTED); cv.text(value,286,y,678,color,FONT_BODY_2X,.9f);
}
#if HAS_HMS_UI
// One entry's visibility under the errorBadgeFor bar, itemised so the alert
// list can apply it per record: the dot and the list agree on what counts as
// an alert, and records the Bambu app does not surface stay off the device.
bool alertEntryVisible(const BambuState& s, uint8_t hmsIndex) {
  const HmsEntry& e = s.hms[hmsIndex];
  const uint8_t sev = hmsSeverityOf(e.code);
  if (sev < 1 || sev > 3) return false;                        // info/unknown: never alerts
  if (sev == 3 && !dispSettings.hmsSeverityAll) return false;   // "important only" default
  if (hmsIsBaseline(s, e.attr, e.code)) return false;           // standing before we connected
  if (hmsIsSelfResolved(e.attr, e.code)) return false;          // printer handled it (#181)
  if (hmsIsRejectedCommand(e.attr, e.code) && !s.hmsOwnCmdRejected) return false;  // (#185)
  return true;
}
#endif
void details(Canvas& cv,const PrinterSlot& p,const P4DashboardView& v,time_t now,const char* deviceIp,bool networkOnline) {
  const auto& s=p.state;
  if (v.page==4) {
    cv.text("看板设置",40,100,900,FG,FONT_BODY_2X);
    row(cv,"显示亮度 / 网络","使用浏览器打开看板配置页面",175);
    char url[80]; snprintf(url,sizeof(url),"http://%s",deviceIp && deviceIp[0]?deviceIp:"bambuhelper.local");
    cv.text(url,48,235,920,GREEN,FONT_LARGE_2X);
    cv.text("扫描二维码打开配置",48,305,610,MUTED);
    // LGFX 1.2.24's C getModule returns an unnormalised byte mask through a
    // C++ bool ABI. Read the documented bit grid directly (also correct on PC).
    QRCode qr; uint8_t modules[137]; // version 4: ceil(33*33/8)
    if (lgfx_qrcode_initText(&qr, modules, 4, ECC_LOW, url) == 0) {
      cv.g.fillRect(748,185,205,205,0xffff);
      for (int y=0;y<qr.size;++y) for (int x=0;x<qr.size;++x) {
        const int bit=y*qr.size+x;
        if (modules[bit/8] & (1u<<(7-bit%8))) cv.g.fillRect(768+x*5,205+y*5,5,5,0x0000);
      }
    }
    row(cv,"触摸操作","顶部 info 查看详情 · 点击图片切换实景与机型",397);
    row(cv,"多台打印机","首页顶部名称区域：切换打印机",453);
    cv.text("顶部返回键关闭 · Gauges 样式保留原有整屏手势",48,539,920,MUTED,FONT_SMALL_2X,.9f);
    return;
  }
  const char* tabs[]={"任务","耗材","设备","提醒"};
  for(int i=0;i<4;++i) {
    int x=40+i*240;
    cv.g.fillRoundRect(x,TAB_Y,224,TAB_H,12,v.page==i?rgb(0x203b33):SURFACE);
    // Middle datum on the pill's centreline — a top-left datum made the
    // label sit visibly low in the 50px button.
    cv.text(tabs[i],x+82,TAB_Y+TAB_H/2,130,v.page==i?GREEN:MUTED,FONT_SMALL_2X,1,v.page==i?rgb(0x203b33):SURFACE,true);
  }
  char b[180];
  if(v.page==0) {
    char title[128]={};
#if defined(P4_CLOUD_CAMERA_EXPERIMENTAL)
    p4CameraJobName(p,title,sizeof(title));
#endif
    cv.text(title[0]?title:s.subtaskName[0]?jobDisplayName(s):"等待新的打印任务",48,161,920,FG,FONT_BODY_2X);
    row(cv,"当前状态",stateName(s,networkOnline),225,accent(s));
    snprintf(b,sizeof(b),"%u / %u",s.layerNum,s.totalLayers); row(cv,"打印层数",s.totalLayers?b:"—",284);
    if(known(s,P4_REMAINING)&&s.printing) duration(b,sizeof(b),s.remainingMinutes); else snprintf(b,sizeof(b),"—");
    row(cv,"预计剩余",b,343);
    const char* stage=runningStageLabel(s); row(cv,"当前阶段",stage?stage:stateName(s,networkOnline),402);
    snprintf(b,sizeof(b),"%u",s.plateIdx); row(cv,"打印盘",s.plateIdx?b:"—",461);
  } else if(v.page==1) {
    ams(cv,s,v.amsPage,48,169,928,true);
  } else if(v.page==2) {
    row(cv,"打印机型号",modelFor(p.config).name,170);
    const char* ip=s.localIp[0]?s.localIp:p.config.ip;
    row(cv,"打印机地址",ip[0]?ip:"未上报",228);
    row(cv,"连接方式",isCloudMode(p.config.mode)?"Bambu 云端":"局域网直连",286);
    row(cv,"无线信号",wifiQuality(s.wifiSignal),344);
    row(cv,"机门状态",!s.doorSensorPresent?"未上报":s.doorOpen?"已打开":"已关闭",402);
    row(cv,"看板固件",FW_VERSION,460);
  } else {
#if HAS_HMS_UI
    // Visible set only: a stopped-print error first (a cancel is the user's
    // own doing and shows as a state word instead), then the HMS records that
    // clear the alertEntryVisible bar. Filtered records are not listed at
    // all — the Bambu app does not surface them either.
    int8_t visible[1 + HMS_MAX_ENTRIES]; unsigned count = 0;
    if (s.printError && !printErrorIsCancel(s.printError)) visible[count++] = -1;
    for (uint8_t i = 0; i < s.hmsCount; ++i)
      if (alertEntryVisible(s, i)) visible[count++] = (int8_t)i;
    if(!count) {
      cv.text("当前没有打印机提醒",48,196,920,GREEN,FONT_BODY_2X);
      cv.text(s.connected?"后续上报的故障与提醒会显示在这里":"连接已断开，提醒状态可能不是最新",48,258,920,MUTED);
    } else {
      const unsigned offset=std::min<unsigned>(v.alertOffset,count-1);
      for(unsigned i=offset;i<std::min<unsigned>(count,offset+2);++i) {
        const int y=166+(i-offset)*175; const char* description=nullptr;
        if(visible[i]<0) { printErrorFormatCode(s.printError,b,sizeof(b)); description=printErrorLookupText(s.printError); }
        else { const auto& e=s.hms[visible[i]]; hmsFormatCode(e.attr,e.code,b,sizeof(b)); description=hmsLookupText(e.attr,e.code); }
        cv.text(b,48,y,920,ORANGE,FONT_BODY_2X);
        if(!description) description="查看网页中的完整提醒说明";
        // Wrap without breaking UTF-8; English catalogue text uses measured words.
        char wrapped[512]; snprintf(wrapped,sizeof(wrapped),"%s",description);
        char* cursor=wrapped;
        for(int line=0;line<3&&*cursor;++line) {
          char* end=cursor; char* lastSpace=nullptr;
          cv.g.setFont(cachedFont(FONT_SMALL_2X)); cv.g.setTextSize(.9f);
          while(*end) { if(*end==' ')lastSpace=end; char saved=end[1]; end[1]=0; int w=cv.g.textWidth(cursor); end[1]=saved; if(w>900)break; ++end; }
          if(*end&&lastSpace&&lastSpace>cursor)end=lastSpace;
          if(end==cursor)++end;
          char saved=*end; *end=0; cv.text(cursor,48,y+45+line*29,920,MUTED,FONT_SMALL_2X,.9f); *end=saved; cursor=end; while(*cursor==' ')++cursor;
        }
      }
      snprintf(b,sizeof(b),"%u–%u / %u · 上下滑动查看更多",offset+1,std::min<unsigned>(count,offset+2),count);
      cv.text(b,48,539,920,MUTED,FONT_SMALL_2X,.9f);
    }
#endif
  }
  if(v.page!=1 && v.page!=3) cv.text("左右滑动切页 · 点顶部箭头返回看板",48,553,920,MUTED,FONT_SMALL_2X,.85f);
  (void)now;
}
}

uint8_t p4AmsPageCount(const AmsState& a) {
  uint8_t count=a.vtPresent?1:0;
  for(uint8_t i=0;i<a.unitCount&&i<AMS_MAX_UNITS;++i) if(a.units[i].present) ++count;
  return count;
}
void followP4ActiveAms(P4DashboardView& v,const AmsState& a){
  if(v.amsManual)return;
  uint8_t page=0;
  for(uint8_t i=0;i<a.unitCount&&i<AMS_MAX_UNITS;++i){
    if(!a.units[i].present)continue;
    if((a.activeTray<AMS_MAX_TRAYS&&a.activeTray/4==i)||
       (a.activeTray==AMS_TRAY_OVERFLOW&&a.ovUnitId==a.units[i].id)){v.amsPage=page;return;}
    ++page;
  }
  if(a.activeTray==254&&a.vtPresent)v.amsPage=page;
}
P4Action p4DashboardGesture(P4DashboardView& v,const BambuState& s,p4ui::Gesture gesture,p4ui::Point p) {
  using namespace p4ui;
  if(gesture==Gesture::None || p.x<0 || p.x>=W || p.y<0 || p.y>=H)return P4Action::None;
  const int pages=p4AmsPageCount(s.ams);
  if(gesture==Gesture::Left||gesture==Gesture::Right) {
    const int step=gesture==Gesture::Left?1:-1;
    if(v.page<0) {
      if(imageContains(p)) {v.showCamera=!v.showCamera;v.cameraChosen=true;}
      else if(homeAmsContains(p)&&pages) {v.amsPage=(v.amsPage+pages+step)%pages;v.amsManual=true;}
    }
    else if(v.page==1&&p.y>=150) { if(pages){v.amsPage=(v.amsPage+pages+step)%pages;v.amsManual=true;} }
    else if(v.page<4) { v.page=(v.page+4+step)%4; v.alertOffset=0; }
    return P4Action::None;
  }
  if(gesture==Gesture::Up||gesture==Gesture::Down) {
#if HAS_HMS_UI
    int count=s.hmsCount+(s.printError?1:0);
    if(v.page==3) v.alertOffset=std::max(0,std::min(std::max(0,count-2),v.alertOffset+(gesture==Gesture::Up?2:-2)));
#endif
    return P4Action::None;
  }
  if(p.y<68) {
    if(p.x>=SETTINGS_X-13&&p.x<W) v.page=4;
    else if(p.x>=DETAILS_X&&p.x<DETAILS_X+DETAILS_W&&p.y>=DETAILS_Y&&p.y<DETAILS_Y+DETAILS_H)v.page=v.page>=0?-1:0;
    else if(p.x<790&&v.page<0)return P4Action::NextPrinter;
    return P4Action::None;
  }
  if(v.page>=0) {
    if(v.page<4&&p.y>=TAB_Y&&p.y<TAB_Y+TAB_H&&p.x>=40&&p.x<1000) { v.page=(p.x-40)/240; v.alertOffset=0; }
    else if(v.page==1&&p.y>=155&&p.y<210&&p.x>850&&pages) {v.amsPage=(v.amsPage+pages+(p.x<920?-1:1))%pages;v.amsManual=true;}
  }
  else if(imageContains(p)) {
    v.showCamera=!v.showCamera; v.cameraChosen=true;
  }
  else if(p.x>=CONTENT&&p.y>=AMS_Y) {
    if(p.y<AMS_Y+40&&p.x>870&&pages){v.amsPage=(v.amsPage+pages+(p.x<936?-1:1))%pages;v.amsManual=true;}
  }
  return P4Action::None;
}
void drawP4Dashboard(lgfx::LovyanGFX& g,const PrinterSlot& p,const P4DashboardView& v,time_t now,const char* deviceIp,bool networkOnline,uint32_t progressPhase) {
  static bool logged = false;
  if (!logged) {
    logged = true;
    Serial.printf("[p4draw] target=%dx%d W=%d H=%d mul=%d/%d off=%d sx1024=%d sy600=%d\n",
                  (int)g.width(), (int)g.height(), p4ui::W, p4ui::H,
                  p4ui::UI_MUL, p4ui::UI_DIV, p4ui::UI_OFFSET_X,
                  p4ui::sx(1024), p4ui::sy(600));
  }
  ScaledGfx scaled(g);
  Canvas cv(scaled);
  cv.g.fillScreen(BG); scaled.setTextWrap(false);
  header(cv,p,v,now,deviceIp);
  if(v.page<0) { printerPanel(cv,p,v,now); job(cv,p,now,networkOnline,progressPhase); ams(cv,p.state,v.amsPage,CONTENT,AMS_Y,RIGHT-CONTENT,false); }
  else details(cv,p,v,now,deviceIp,networkOnline);
  g.setTextSize(1); g.setFont(nullptr);
}
#endif
