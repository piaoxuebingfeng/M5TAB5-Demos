#pragma once
#include <cstdint>

// A working capture repeats every five minutes; a failed one retries after a
// minute. There is deliberately no escalating backoff: the live image is the
// point of the view, and a failure that parks the previous frame for eight
// minutes reads as "the picture is stuck". The burst guard is the attempt
// clock itself - every attempt, successful or not, advances the scheduler (see
// cameraTask), so a broken capture retries once a minute instead of spinning.
constexpr uint32_t p4CameraRefreshMs() { return 300000u; }
constexpr uint32_t p4CameraRetryMs()   { return 60000u; }

constexpr uint32_t p4CameraRetryDelayMs(unsigned failures) {
  return failures == 0 ? p4CameraRefreshMs() : p4CameraRetryMs();
}

constexpr bool p4CameraRetryDue(uint32_t now, uint32_t previous, unsigned failures) {
  return uint32_t(now - previous) >= p4CameraRetryDelayMs(failures);
}

constexpr bool p4CameraRetryDue(uint32_t now, uint32_t previous, unsigned failures,
                                bool attempted) {
  return attempted && p4CameraRetryDue(now, previous, failures);
}

// A visible printer without a current image needs its first capture as soon as
// MQTT and the network are ready. Only failures start the missing-image backoff.
constexpr bool p4CameraMissingImageDue(uint32_t now, uint32_t previousAttempt,
                                       unsigned failures) {
  return !previousAttempt || p4CameraRetryDue(now, previousAttempt, failures, true);
}
