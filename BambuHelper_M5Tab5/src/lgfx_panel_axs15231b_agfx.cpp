// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

// Out-of-line definition for Panel_AXS15231B_AGFX::_te_sem. Kept in a
// separate .cpp (rather than `inline` in the header) because the project
// is compiled in C++14 mode, which does not support inline static data
// members. See lgfx_panel_axs15231b_agfx.hpp for rationale.
//
// Guarded behind BOARD_IS_JC3248W535 so other board envs (which do not
// depend on Arduino_GFX_Library.h) skip this translation unit entirely.

#if defined(BOARD_IS_JC3248W535)

#include "lgfx_panel_axs15231b_agfx.hpp"

SemaphoreHandle_t lgfx::v1::Panel_AXS15231B_AGFX::_te_sem = nullptr;

#endif
