#pragma once
#if defined(BOARD_IS_WS_P4_7B)
#ifndef P4_SETTINGS_OWNER
#define P4_SETTINGS_OWNER p4UiSettings
#endif
// C++ references preserve normal field/parameter lookup; these are not text
// macros. Each translation unit chooses exactly one settings owner.
namespace {
auto& printers = P4_SETTINGS_OWNER.printers;
auto& rotState = P4_SETTINGS_OWNER.rotState;
auto& netSettings = P4_SETTINGS_OWNER.netSettings;
auto& dispSettings = P4_SETTINGS_OWNER.dispSettings;
auto& gaugeLabels = P4_SETTINGS_OWNER.gaugeLabels;
auto& dpSettings = P4_SETTINGS_OWNER.dpSettings;
auto& buttonType = P4_SETTINGS_OWNER.buttonType;
auto& buttonPin = P4_SETTINGS_OWNER.buttonPin;
auto& buzzerSettings = P4_SETTINGS_OWNER.buzzerSettings;
auto& ledSettings = P4_SETTINGS_OWNER.ledSettings;
auto& tasmotaSettings = P4_SETTINGS_OWNER.tasmotaSettings;
auto& tasmotaCurrency = P4_SETTINGS_OWNER.tasmotaCurrency;
auto& tasmotaTariffPerKwh = P4_SETTINGS_OWNER.tasmotaTariffPerKwh;
auto& wifiSSID = P4_SETTINGS_OWNER.wifiSSID;
auto& wifiPass = P4_SETTINGS_OWNER.wifiPass;
auto& p4PanelBrightness = P4_SETTINGS_OWNER.brightness;
auto& activePrinterIndex = P4_SETTINGS_OWNER.activePrinterIndex;
auto& dualPrinterUnsafe = P4_SETTINGS_OWNER.dualPrinterUnsafe;
auto& quadPrinterBeta = P4_SETTINGS_OWNER.quadPrinterBeta;
auto& wifiTxCapped = P4_SETTINGS_OWNER.wifiTxCapped;
}
#endif
