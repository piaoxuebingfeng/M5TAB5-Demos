#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// Field-level three-way merge. Only values changed by the form replace the
// newest background state; settings are plain, trivially copyable values.
template<typename T>
inline void p4MergeField(T& current, const T& before, const T& after) {
  static_assert(std::is_trivially_copyable<T>::value, "settings must be values");
  if (std::memcmp(&before, &after, sizeof(T)) != 0)
    std::memcpy(&current, &after, sizeof(T));
}

template<typename Settings>
inline void p4MergeSettingsInPlace(Settings& merged, const Settings& current, const Settings& before,
                                   const Settings& after, uint8_t printerCount) {
  for (uint8_t i = 0; i < printerCount; ++i) {
    p4MergeField(merged.printers[i].config, before.printers[i].config, after.printers[i].config);
    // cloudUserId is derived from the token and serial, not edited by the
    // normal printer form. Keep a freshly resolved background value when the
    // rest of the form changes. Explicit imports may still replace it.
    auto& id = merged.printers[i].config.cloudUserId;
    if (std::memcmp(before.printers[i].config.cloudUserId, after.printers[i].config.cloudUserId,
                    sizeof(id)) != 0) {
      std::memcpy(id, after.printers[i].config.cloudUserId, sizeof(id));
    } else if (std::memcmp(before.printers[i].config.serial, after.printers[i].config.serial,
                           sizeof(before.printers[i].config.serial)) != 0 ||
               before.printers[i].config.region != after.printers[i].config.region) {
      id[0] = 0;
    } else {
      std::memcpy(id, current.printers[i].config.cloudUserId, sizeof(id));
    }
  }
  p4MergeField(merged.rotState, before.rotState, after.rotState);
  p4MergeField(merged.netSettings, before.netSettings, after.netSettings);
  p4MergeField(merged.dispSettings, before.dispSettings, after.dispSettings);
  p4MergeField(merged.gaugeLabels, before.gaugeLabels, after.gaugeLabels);
  p4MergeField(merged.dpSettings, before.dpSettings, after.dpSettings);
  p4MergeField(merged.buttonType, before.buttonType, after.buttonType);
  p4MergeField(merged.buttonPin, before.buttonPin, after.buttonPin);
  p4MergeField(merged.buzzerSettings, before.buzzerSettings, after.buzzerSettings);
  p4MergeField(merged.ledSettings, before.ledSettings, after.ledSettings);
  for (size_t i = 0; i < sizeof(merged.tasmotaSettings) / sizeof(merged.tasmotaSettings[0]); ++i)
    p4MergeField(merged.tasmotaSettings[i], before.tasmotaSettings[i], after.tasmotaSettings[i]);
  p4MergeField(merged.tasmotaCurrency, before.tasmotaCurrency, after.tasmotaCurrency);
  p4MergeField(merged.tasmotaTariffPerKwh, before.tasmotaTariffPerKwh, after.tasmotaTariffPerKwh);
  p4MergeField(merged.wifiSSID, before.wifiSSID, after.wifiSSID);
  p4MergeField(merged.wifiPass, before.wifiPass, after.wifiPass);
  p4MergeField(merged.brightness, before.brightness, after.brightness);
  p4MergeField(merged.activePrinterIndex, before.activePrinterIndex, after.activePrinterIndex);
  p4MergeField(merged.dualPrinterUnsafe, before.dualPrinterUnsafe, after.dualPrinterUnsafe);
  p4MergeField(merged.quadPrinterBeta, before.quadPrinterBeta, after.quadPrinterBeta);
  p4MergeField(merged.wifiTxCapped, before.wifiTxCapped, after.wifiTxCapped);
}

template<typename Settings>
inline Settings p4MergeSettings(const Settings& current, const Settings& before,
                                const Settings& after, uint8_t printerCount) {
  Settings merged = current;
  p4MergeSettingsInPlace(merged, current, before, after, printerCount);
  return merged;
}
