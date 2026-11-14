#ifndef APP_MAIN_H
#define APP_MAIN_H

// Application layer ported from BambuHelper's main.cpp.
//
// The sketch owns setup()/loop() (Arduino's build refuses to link if a .cpp
// defines them too), so the two entry points are renamed and called from
// BambuHelper_M5Tab5.ino. Everything else in app_main.cpp keeps its original
// name and shape, which makes future diffs against upstream readable.
//
// Board bring-up that is Tab5-specific (PI4IOE5V6408 rails and resets, the C6
// SDIO pin map) runs in the sketch *before* appSetup(), because appSetup()
// starts from "hardware is powered" just like it did on the 7B board.

void appSetup();
void appLoop();

#endif  // APP_MAIN_H
