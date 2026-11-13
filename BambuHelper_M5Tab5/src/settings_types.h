#pragma once

#include "config.h"
#include <cstddef>
#include <cstdint>

// Plain persisted values shared by the renderer, storage and web workers.
// This header deliberately has no Arduino, NVS, alias or worker dependencies.
enum : uint8_t { GT_SMART = 0, GT_NEXT = 1, GT_AMS = 2, GT_WAKE = 3 };
enum : uint8_t { GD_OFF = 0, GD_PLUG = 1, GD_AMS = 2, GD_NEXT = 3 };
enum : uint8_t { GH_LED = 0, GH_AMS = 1, GH_NONE = 2 };

struct GaugeColors {
  uint16_t arc;
  uint16_t label;
  uint16_t value;
};

struct DisplaySettings {
  uint8_t  rotation;
  uint16_t bgColor;
  uint16_t trackColor;
  uint16_t progressBarColor;
  bool     animatedBar;
  bool     pongClock;
  bool     smallLabels;
  uint8_t  timeDisplayMode;
  bool     fanMatchPrinter;
  bool     hideStatusReadout;
  bool     invertColors;
  bool     cydPanelClassic;
  bool     cyd32eVariant;
  uint8_t  roundSkin;
  uint8_t  cardStyle;
  uint8_t  cardLeft;
  uint8_t  cardBottom;
  uint8_t  cardReady;
  bool     landscape8Slots;
  bool     portrait9Slots;
  uint16_t clockTimeColor;
  uint16_t clockDateColor;
  uint8_t  clockTimeSize;
  uint8_t  clockDateSize;
  bool     hideClockDate;
  bool     showClockInfo;
  bool     amsTrayTypes;
  bool     buttonPowerControl;
  uint8_t  gestTap;
  uint8_t  gestDouble;
  uint8_t  gestHold;
  bool     showBatteryIndicator;
  uint8_t  glowMode;
  uint16_t glowColor;
  uint8_t  glowStyle;
  uint8_t  glowDuration;
#if HAS_HMS_UI
  bool     hmsEnabled;
  bool     hmsSeverityAll;
  uint8_t  hmsAlertMask;
  uint8_t  hmsAutoPresent;
  bool     hmsLookupOnline;
#endif
  uint16_t nozzleScaleMax;
  uint16_t bedScaleMax;
  uint16_t chamberScaleMax;
  uint16_t powerScaleW;
  uint8_t  gaugeSmoothing;
  uint16_t warnColor;
  uint8_t  warnThresholdPct;
  uint16_t etaColor;
  uint16_t finishColor;
  uint16_t statusOkColor;
  uint16_t printerNameColor;
  uint16_t doorClosedColor;
  uint16_t doorOpenColor;
  uint16_t textColor;
  uint16_t textDimColor;
  GaugeColors progress;
  GaugeColors nozzle;
  GaugeColors bed;
  GaugeColors partFan;
  GaugeColors auxFan;
  GaugeColors auxFanRight;
  GaugeColors chamberFan;
  GaugeColors exhaustFan;
  GaugeColors chamberTemp;
  GaugeColors heatbreak;
  GaugeColors power;
  GaugeColors layer;
};

static const uint8_t GAUGE_LABEL_LEN = 37;
struct GaugeLabels {
  char progress[GAUGE_LABEL_LEN];
  char nozzle[GAUGE_LABEL_LEN];
  char nozzleRight[GAUGE_LABEL_LEN];
  char nozzleLeft[GAUGE_LABEL_LEN];
  char bed[GAUGE_LABEL_LEN];
  char partFan[GAUGE_LABEL_LEN];
  char auxFan[GAUGE_LABEL_LEN];
  char auxFanRight[GAUGE_LABEL_LEN];
  char chamberFan[GAUGE_LABEL_LEN];
  char exhaustFan[GAUGE_LABEL_LEN];
  char chamberTemp[GAUGE_LABEL_LEN];
  char heatbreak[GAUGE_LABEL_LEN];
  char power[GAUGE_LABEL_LEN];
  char layer[GAUGE_LABEL_LEN];
  char clock[GAUGE_LABEL_LEN];
  char amsBase[GAUGE_LABEL_LEN];
  char door[GAUGE_LABEL_LEN];
};

struct NetworkSettings {
  bool useDHCP;
  char staticIP[16];
  char gateway[16];
  char subnet[16];
  char dns[16];
  bool showIPAtStartup;
  uint8_t timezoneIndex;
  char timezoneStr[64];
  bool use24h;
  uint8_t dateFormat;
  bool mdnsEnabled;
  char hostname[32];
};

struct DisplayPowerSettings {
  uint16_t finishDisplayMins;
  bool keepDisplayOn;
  bool showClockAfterFinish;
  bool doorAckEnabled;
  bool keepPrintScreen;
  bool finishShowTime;
  bool nightModeEnabled;
  uint8_t nightStartHour;
  uint8_t nightEndHour;
  uint8_t nightBrightness;
  uint8_t screensaverBrightness;
};

enum ButtonType : uint8_t { BTN_DISABLED = 0, BTN_PUSH = 1, BTN_TOUCH = 2, BTN_TOUCHSCREEN = 3 };

struct BuzzerSettings {
  bool enabled;
  uint8_t pin;
  uint8_t quietStartHour;
  uint8_t quietEndHour;
  bool buttonClick;
  bool bedCooldownAlert;
  uint8_t bedCooldownThresholdC;
};

enum LedFinishMode : uint8_t {
  LED_FINISH_OFF = 0,
  LED_FINISH_BREATHING = 1,
  LED_FINISH_HEARTBEAT = 2,
};

enum LedDriver : uint8_t {
  LED_DRV_SINGLE = 0,
  LED_DRV_RGB = 1,
  LED_DRV_PIXEL = 2,
};

struct LedSettings {
  bool enabled;
  uint8_t pin;
  uint8_t brightness;
  uint8_t finishMode;
  uint16_t finishSeconds;
  uint8_t finishBrightness;
  bool autoOnWhilePrinting;
  bool pauseBreathing;
  bool errorStrobe;
  uint16_t errorStrobeSeconds;
  uint8_t driver;
  uint8_t pinG;
  uint8_t pinB;
  bool commonAnode;
  uint32_t colorIdle;
  uint32_t colorPrinting;
  uint32_t colorPaused;
  uint32_t colorFinished;
  uint32_t colorError;
  bool nightOff;
};

#ifndef TASMOTA_PLUG_COUNT
#ifdef BOARD_LOW_RAM
#define TASMOTA_PLUG_COUNT 1
#else
#define TASMOTA_PLUG_COUNT MAX_ACTIVE_PRINTERS
#endif
#endif

struct TasmotaSettings {
  bool enabled;
  uint8_t plugType;
  uint8_t plugOutlet;
  uint8_t plugOutletExtra;
  char ip[16];
  uint8_t displayMode;
  uint8_t pollInterval;
#if TASMOTA_PLUG_COUNT == 1
  uint8_t assignedSlot;
#endif
  bool autoOffEnabled;
  uint8_t autoOffDelayMin;
  bool autoOffCancelOnDoor;
};
