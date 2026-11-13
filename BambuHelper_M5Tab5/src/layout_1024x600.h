#ifndef LAYOUT_1024x600_H
#define LAYOUT_1024x600_H

// Waveshare LCD-7B: native landscape, six large gauges. Rotation 0/180.
// Shared screen padding/icons follow the 2x font tier as well.
#define LY_SC(v) ((v) * 2)

// --- Screen dimensions ---
#define LY_W    1024
#define LY_H    600

// --- LED progress bar (top, y=0) ---
#define LY_BAR_W   1008
#define LY_BAR_H   12

// --- Header bar ---
#define LY_HDR_Y        18
#define LY_HDR_H        46
#define LY_HDR_NAME_X   24
#define LY_HDR_CY       41
#define LY_HDR_BADGE_RX 24
#define LY_HDR_DOT_CY   25

// --- Printing: 2x3 gauge grid ---
#define LY_GAUGE_R   88
#define LY_GAUGE_T   14
#define LY_TEMP_GAUGE_T 10        // temp/fan/humidity arc thickness
#define LY_GAUGE_VALUE_FONT FONT_LARGE  // primary value font (Inter 37pt on LCD-7B)
#define LY_GAUGE_VALUE_NUDGE_Y 0        // extra Y shift for primary value when
                                        // a secondary line is shown below it
#define LY_COL1      178
#define LY_COL2      512
#define LY_COL3      846
#define LY_ROW1      164
#define LY_ROW2      382
#define LY_LAND8_COL1 128
#define LY_LAND8_COL2 384
#define LY_LAND8_COL3 640
#define LY_LAND8_COL4 896
#define LY_LAND_ROW1 LY_ROW1
#define LY_LAND_ROW2 LY_ROW2

// Optional AMS strip replaces the second gauge row. The full AMS page is
// available by touch independently of this saved dashboard preference.
#define LY_AMS_Y          294
#define LY_AMS_H          180
#define LY_AMS_BAR_H      120
#define LY_AMS_BAR_GAP    10
#define LY_AMS_GROUP_GAP  28
#define LY_AMS_LABEL_OFFY 8
#define LY_AMS_MARGIN     24
#define LY_AMS_BAR_MAX_W  100
#define LY_AMS_BAR_MAX_W_EXTRAS 100

// Native landscape split: two 512x600 printer panels, four gauges each.
#define LAYOUT_HAS_SPLIT 1
#define LAYOUT_HAS_SPLIT_LANDSCAPE 1
#define LY_SPLIT_L_SLOTS 4
#define LY_SPLIT_L_NCOLS 2
#define LY_SPLIT_L_GAUGE_R 88
#define LY_SPLIT_L_GAUGE_T LY_GAUGE_T
#define LY_SPLIT_L_BAR_H 10
#define LY_SPLIT_L_BAR_MARGIN 20
#define LY_SPLIT_L_BAR_Y 12
#define LY_SPLIT_L_HDR_CY 54
#define LY_SPLIT_L_ROW1 178
#define LY_SPLIT_L_ROW2 400
#define LY_SPLIT_L_COL1 128
#define LY_SPLIT_L_COL2 384
// Keep the generic stacked fallback compilable; P4 always selects landscape.
#define LY_SPLIT_SLOTS 3
#define LY_SPLIT_DIV_Y 300
#define LY_SPLIT_GAUGE_R 70
#define LY_SPLIT_GAUGE_T LY_GAUGE_T
#define LY_SPLIT_BAR_H 10
#define LY_SPLIT_BAR_MARGIN 20
#define LY_SPLIT_A_BAR_Y 12
#define LY_SPLIT_A_HDR_CY 52
#define LY_SPLIT_A_ROW1 166
#define LY_SPLIT_B_BAR_Y 312
#define LY_SPLIT_B_HDR_CY 352
#define LY_SPLIT_B_ROW1 466

// --- Printing: ETA / info zone ---
#define LY_ETA_Y        494
#define LY_ETA_H        38
#define LY_ETA_TEXT_Y   511
#define LY_FILE_Y       547
#define LY_FILE_H       28

// --- Printing: bottom status bar ---
#define LY_BOT_Y    564
#define LY_BOT_H    36
#define LY_BOT_CY   582

// --- Printing: WiFi signal indicator ---
#define LY_WIFI_X    16
#define LY_WIFI_Y    582

// --- Battery indicator placeholders ---
// No battery on LCD-7B; geometry keeps the shared status helpers available.
#define LY_BAT_W       16
#define LY_BAT_H       32
#define LY_BAT_TEXT_X  24
#define LY_BAT_SHIFT_X 28

// --- Idle screen (with printer) ---
#define LY_IDLE_NAME_Y      90
#define LY_IDLE_STATE_Y      145
#define LY_IDLE_STATE_H      50
#define LY_IDLE_STATE_TY     170
#define LY_IDLE_DOT_Y        230
#define LY_IDLE_GAUGE_R      88
#define LY_IDLE_GAUGE_Y      370
#define LY_IDLE_G_OFFSET      210

// --- Idle screen (no printer) ---
#define LY_IDLE_NP_TITLE_Y  140
#define LY_IDLE_NP_WIFI_Y   240
#define LY_IDLE_NP_DOT_Y    270
#define LY_IDLE_NP_MSG_Y    340
#define LY_IDLE_NP_OPEN_Y   420
#define LY_IDLE_NP_IP_Y    505

// --- Finished screen ---
#define LY_FIN_GAUGE_R   88
#define LY_FIN_GL        310
#define LY_FIN_GR        714
#define LY_FIN_GY        210
#define LY_FIN_TEXT_Y    365
#define LY_FIN_FILE_Y    440
#define LY_FIN_BOT_Y    550
#define LY_FIN_BOT_H    50
#define LY_FIN_WIFI_Y   577

// --- AP mode screen ---
#define LY_AP_TITLE_Y     80
#define LY_AP_SSID_LBL_Y 175
#define LY_AP_SSID_Y     255
#define LY_AP_PASS_LBL_Y 340
#define LY_AP_PASS_Y      410
#define LY_AP_OPEN_Y      475
#define LY_AP_IP_Y        535

// --- Simple clock ---
#define LY_CLK_CLEAR_Y   100
#define LY_CLK_CLEAR_H   400
#define LY_CLK_TIME_Y    250
#define LY_CLK_AMPM_Y    350
#define LY_CLK_DATE_Y    420

// --- Pong/Breakout clock ---
#define LY_ARK_BRICK_ROWS   5
#define LY_ARK_COLS          10
#define LY_ARK_BRICK_W      94
#define LY_ARK_BRICK_H      20
#define LY_ARK_BRICK_GAP    6
#define LY_ARK_START_X      15
#define LY_ARK_START_Y      70
#define LY_ARK_PADDLE_Y     560
#define LY_ARK_PADDLE_W     120
#define LY_ARK_TIME_Y       325
#define LY_ARK_DATE_Y       24
#define LY_ARK_DIGIT_W      64
#define LY_ARK_DIGIT_H      96
#define LY_ARK_COLON_W      24
#define LY_ARK_DATE_CLR_X   210
#define LY_ARK_DATE_CLR_W   604

#endif // LAYOUT_1024x600_H
