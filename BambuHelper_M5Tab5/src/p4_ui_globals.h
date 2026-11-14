#pragma once
// Storage and HTTP owners keep legacy field access out of renderer modules.
#if defined(BOARD_IS_WS_P4_7B)
#include "p4_ui_state.h"
#include "p4_legacy_aliases.h"
#include "p4_tasmota_stub.h"
#endif
