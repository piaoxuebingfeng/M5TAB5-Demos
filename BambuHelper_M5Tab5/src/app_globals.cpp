// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

// =============================================================================
//  Globals that used to sit at the top of BambuHelper's main.cpp
// =============================================================================
//
// main.cpp is parked in reference/ because the sketch owns setup()/loop(), so
// anything it defined with file scope has to live somewhere else. Only the
// symbols the ported sources actually reference are listed here; the app layer
// itself arrives in a later stage and will pull the rest in with it.

#include "settings.h"

// Renderer-facing snapshot of the UI settings. Display code reads it directly
// (display_ui.cpp, applyPanelInversion()), so it must be a real definition and
// not a per-translation-unit static.
P4UiSettings p4UiSettings;
