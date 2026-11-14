#pragma once
#if defined(BOARD_IS_WS_P4_7B)
// Call once from the Wi-Fi owner before ESP-Hosted creates its tasks.
void p4HostedMemoryStart();
void p4HostedMemoryLog();
#endif
