#pragma once

#include <ArduinoJson.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "bambu_state.h"

// Firmware generations mix JSON strings and numbers. A missing/null field
// leaves the cached reading alone; a reported numeric zero is a real reading.
inline bool readAmsNumber(JsonVariantConst field, double& value) {
  if (field.is<const char*>()) {
    const char* text = field.as<const char*>();
    char* end = nullptr;
    value = strtod(text, &end);
    if (end == text) return false;
    while (*end == ' ' || *end == '\t') ++end;
    return !*end && std::isfinite(value);
  }
  if (!field.is<double>()) return false;
  value = field.as<double>();
  return std::isfinite(value);
}

inline bool readAmsId(JsonVariantConst field, uint8_t& id) {
  double value;
  if (!readAmsNumber(field, value) || value < 0 || value > 255 || value != floor(value)) return false;
  id = static_cast<uint8_t>(value);
  return true;
}

inline int findAmsUnit(const AmsState& state, uint8_t id) {
  for (uint8_t i = 0; i < state.unitCount && i < AMS_MAX_UNITS; ++i)
    if (state.units[i].present && state.units[i].id == id) return i;
  return -1;
}

inline void mergeAmsTelemetry(AmsUnit& unit, JsonObjectConst fields) {
  double value;
  if (readAmsNumber(fields["humidity"], value) && value >= 0 && value <= 100) {
    if (value > 5) {
      unit.humidityRaw = static_cast<uint8_t>(value);
      unit.humidityRawKnown = true;
    } else unit.humidity = static_cast<uint8_t>(value);
  }
  if (readAmsNumber(fields["humidity_raw"], value) && value >= 0 && value <= 100) {
    unit.humidityRaw = static_cast<uint8_t>(value);
    unit.humidityRawKnown = true;
  }
  if (readAmsNumber(fields["temp"], value)) unit.temp = static_cast<float>(value);
  if (readAmsNumber(fields["dry_time"], value) || readAmsNumber(fields["left_dry_time"], value)) {
    if (value >= 0 && value <= 65535) {
      const uint16_t remain = static_cast<uint16_t>(value);
      if (remain > 0 && (unit.dryRemainMin == 0 || remain > unit.dryTotalMin)) unit.dryTotalMin = remain;
      if (remain == 0) unit.dryTotalMin = 0;
      unit.dryRemainMin = remain;
    }
  }
}

inline void mergeAmsUnits(AmsState& state, JsonArrayConst units, bool snapshot) {
  AmsState previous = state;
  if (snapshot && units.size()) {
    memset(state.units, 0, sizeof(state.units));
    state.unitCount = 0;
  }
  for (JsonObjectConst fields : units) {
    uint8_t id;
    if (!readAmsId(fields["id"], id)) continue;
    int index = findAmsUnit(state, id);
    if (index < 0) {
      if (state.unitCount >= AMS_MAX_UNITS) continue;
      index = state.unitCount++;
      const int oldIndex = findAmsUnit(previous, id);
      state.units[index] = oldIndex >= 0 ? previous.units[oldIndex] : AmsUnit{};
    }
    AmsUnit& unit = state.units[index];
    unit.present = true;
    unit.id = id;
    mergeAmsTelemetry(unit, fields);
  }
  state.trayUnitCount = state.unitCount < AMS_TRAY_UNITS ? state.unitCount : AMS_TRAY_UNITS;
  state.anyDrying = false;
  for (uint8_t i = 0; i < state.unitCount; ++i)
    if (state.units[i].present && state.units[i].dryRemainMin) state.anyDrying = true;
}
