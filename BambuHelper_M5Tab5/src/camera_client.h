// ===========================================================================
//  camera_client.h  -  Bambu P1/A1 LAN camera (issue #120)
// ---------------------------------------------------------------------------
//  Streams the chamber-image MJPEG from a LAN-mode P1/A1 printer (port 6000,
//  TLS, "bblp" auth, FFD8..FFD9 frames) and exposes the latest complete JPEG
//  for the UI to decode with tft.drawJpg.
//
//  Real implementation is gated to BOARD_HAS_CAMERA (jc3248w535 / ws_lcd_350,
//  i.e. PSRAM + touch). Every other board gets no-op stubs so all envs link.
// ===========================================================================
#ifndef CAMERA_CLIENT_H
#define CAMERA_CLIENT_H

#include <Arduino.h>
#include <stddef.h>

// --- Gates -----------------------------------------------------------------
// True only when a 2nd/3rd TLS socket to a P1/A1 camera is sane: camera board,
// at most 2 live MQTT connections (3 TLS, the proven ceiling), displayed printer
// is a LAN-mode P1/A1 with an access code. Called by every draw/service path.
bool cameraCanStreamDisplayedPrinter();

// True when the displayed printer has a GAUGE_CAMERA tile in a slot that is
// actually visible in the current layout (standard grid always; extras only in
// their active 8/9-slot mode).
bool cameraDisplayedHasCameraTile();

// True if not streaming, or streaming the IP of the currently displayed printer.
// The loop stops a mismatched stream so frames never belong to a stale printer.
bool cameraStreamingDisplayed();

// --- Lifecycle -------------------------------------------------------------
void cameraBegin();    // start streaming the displayed printer (no-op if gate fails)
void cameraStop();     // tear down socket + free buffers (returns heap to baseline)
bool cameraActive();

// Per-loop bounded, non-blocking socket service. No-op when inactive.
void cameraService();

// Read-only pointer to the latest fully published JPEG frame. Stable until the
// next cameraService() publishes a new one (single-loop model: draw and service
// never overlap). frameId is monotonic so renderers can detect a new frame.
// Returns false if no frame yet.
bool cameraGetLatestFrame(const uint8_t** buf, size_t* len, uint32_t* frameId);

#endif // CAMERA_CLIENT_H
