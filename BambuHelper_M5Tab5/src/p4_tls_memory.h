#pragma once
#if defined(BOARD_IS_WS_P4_7B)
// Install once, before any socket worker starts a TLS context.
void p4TlsMemoryStart();
void p4TlsMemoryLog();
#endif
