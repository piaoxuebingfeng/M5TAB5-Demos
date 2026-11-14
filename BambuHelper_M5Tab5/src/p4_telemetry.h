#pragma once
#if defined(BOARD_IS_WS_P4_7B)
#include <ArduinoJson.h>
#include "bambu_state.h"
#include <cstdlib>
#include <cmath>

inline void mergeP4Nozzle(JsonObjectConst fields, float& diameter, char* type, size_t size) {
  JsonVariantConst value = fields["diameter"];
  float next = 0;
  if (value.is<float>()) next = value.as<float>();
  else if (value.is<const char*>()) {
    const char* text = value.as<const char*>(); char* end = nullptr;
    next = strtof(text, &end);
    if (end == text || *end) next = 0;
  }
  if (std::isfinite(next) && next > 0 && next < 2) diameter = next;
  if (fields["type"].is<const char*>()) strlcpy(type, fields["type"], size);
}
inline void mergeP4NozzleMetadata(JsonObjectConst print, BambuState& s) {
  for (JsonObjectConst entry : print["device"]["nozzle"]["info"].as<JsonArrayConst>()) {
    if (!entry["id"].is<unsigned>()) continue;
    const unsigned id = entry["id"].as<unsigned>();
    if (id > 1) continue;
    mergeP4Nozzle(entry, s.nozzleDiameterN[id], s.nozzleTypeN[id], sizeof(s.nozzleTypeN[id]));
  }
}
#endif
