// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

#include "buzzer_backend.h"
#include "settings.h"

#if defined(BOARD_HAS_NS4168_AUDIO)

// NS4168 I2S class-D mono amplifier backend (Guition JC3248W535).
// Unlike the ES8311 codec (ws_lcd_154) the NS4168 is a "dumb" amp: no I2C
// control interface, no MCLK input, no register setup. It derives its clocks
// from BCLK/LRCLK and plays whatever arrives on SDATA. There is also no
// software-controllable enable pin wired on this board, so power saving is
// achieved by tearing down the I2S driver after an idle timeout - without
// BCLK the amplifier produces no output.

#include <driver/i2s.h>
#include <math.h>

namespace {

// Audio parameters
constexpr uint32_t kSampleRate     = 16000;
constexpr size_t   kFramesPerChunk = 128;           // per DMA buffer
constexpr size_t   kChunkSamples   = kFramesPerChunk * 2;  // stereo
constexpr int16_t  kToneAmplitude  = 9000;

// Envelope for click-free transitions between tones
constexpr uint16_t kEnvMax  = 256;
constexpr uint16_t kEnvStep = 4;   // ~4ms full ramp at 16 kHz / 128 frames

// Idle timeout - shutdown audio pipeline after this much silence
constexpr uint32_t kIdleTimeoutMs = 1500;

// Audio lifecycle state
enum AudioState : uint8_t { AUDIO_OFF = 0, AUDIO_RUNNING };
volatile AudioState gAudioState = AUDIO_OFF;
volatile bool gShutdownRequested = false;
uint32_t gIdleStartMs = 0;

// Tone state - shared between main loop and audio task
volatile uint16_t gCurrentFreq  = 0;
volatile uint32_t gPhaseStep    = 0;
volatile uint16_t gTargetGain   = 0;
volatile uint16_t gCurrentGain  = 0;

// Internal state
bool     gI2sReady    = false;
volatile bool gTaskStarted = false;
uint32_t gPhase       = 0;
uint32_t gLastTonePhaseStep = 0;

int16_t  gWaveTable[256];
bool     gWaveReady = false;
volatile TaskHandle_t gAudioTask = nullptr;

// ----- helpers -----

void buildWaveTable() {
  if (gWaveReady) return;
  for (int i = 0; i < 256; i++)
    gWaveTable[i] = (int16_t)lroundf(sinf(2.0f * PI * i / 256.0f) * 32767.0f);
  gWaveReady = true;
}

// ----- I2S -----

bool initI2s() {
  if (gI2sReady) return true;

  i2s_config_t cfg = {};
  cfg.mode            = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate     = kSampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format  = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count   = 6;
  cfg.dma_buf_len     = kFramesPerChunk;
  cfg.use_apll        = false;
  cfg.tx_desc_auto_clear = true;
  cfg.bits_per_chan    = I2S_BITS_PER_CHAN_16BIT;

  if (i2s_driver_install((i2s_port_t)AUDIO_I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("NS4168: i2s_driver_install failed");
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.mck_io_num    = I2S_PIN_NO_CHANGE;  // NS4168 has no MCLK input
  pins.bck_io_num    = AUDIO_I2S_BCLK;
  pins.ws_io_num     = AUDIO_I2S_LRC;
  pins.data_out_num  = AUDIO_I2S_DOUT;
  pins.data_in_num   = I2S_PIN_NO_CHANGE;

  if (i2s_set_pin((i2s_port_t)AUDIO_I2S_PORT, &pins) != ESP_OK) {
    Serial.println("NS4168: i2s_set_pin failed");
    i2s_driver_uninstall((i2s_port_t)AUDIO_I2S_PORT);
    return false;
  }

  i2s_zero_dma_buffer((i2s_port_t)AUDIO_I2S_PORT);
  gI2sReady = true;
  return true;
}

// ----- tone generation -----

uint32_t phaseStepFor(uint16_t freq) {
  if (freq == 0) return 0;
  return (uint32_t)(((uint64_t)freq << 32) / kSampleRate);
}

void fillChunk(int16_t* out) {
  uint32_t step = gPhaseStep;
  if (step != 0) gLastTonePhaseStep = step;
  else step = gLastTonePhaseStep;

  for (size_t i = 0; i < kFramesPerChunk; i++) {
    uint16_t target = gTargetGain;
    if (gCurrentGain < target) {
      uint16_t next = gCurrentGain + kEnvStep;
      gCurrentGain = (next > target) ? target : next;
    } else if (gCurrentGain > target) {
      gCurrentGain = (gCurrentGain > kEnvStep + target)
                     ? (uint16_t)(gCurrentGain - kEnvStep)
                     : target;
    }

    int16_t sample = 0;
    if (gCurrentGain > 0 && step != 0) {
      uint8_t idx = (uint8_t)(gPhase >> 24);
      int32_t raw = gWaveTable[idx];
      int64_t scaled = (int64_t)raw * kToneAmplitude * gCurrentGain;
      sample = (int16_t)(scaled / ((int64_t)32767 * kEnvMax));
      gPhase += step;
    }
    out[i * 2]     = sample;
    out[i * 2 + 1] = sample;
  }
}

// ----- audio task -----
// Streams tones when requested, silence between tones.
// Exits cooperatively when gShutdownRequested is set.

void audioTask(void*) {
  static int16_t chunk[kChunkSamples];

  while (!gShutdownRequested) {
    fillChunk(chunk);
    size_t written = 0;
    i2s_write((i2s_port_t)AUDIO_I2S_PORT, chunk, sizeof(chunk),
              &written, pdMS_TO_TICKS(50));
  }

  // Clean self-exit
  gTaskStarted = false;
  gAudioTask = nullptr;
  vTaskDelete(nullptr);
}

void ensureTaskStarted() {
  if (gTaskStarted) return;
  gShutdownRequested = false;
  BaseType_t ok = xTaskCreatePinnedToCore(
      audioTask, "buzzer_ns4168", 4096, nullptr, 1, (TaskHandle_t*)&gAudioTask, tskNO_AFFINITY);
  if (ok == pdPASS) {
    gTaskStarted = true;
  } else {
    Serial.println("NS4168: failed to start audio task");
  }
}

// ----- lifecycle management -----

// Amplifier settling time after I2S clocks appear. The NS4168 wakes from its
// clock-loss power-down when BCLK returns; let it stabilize on streamed
// silence before the first tone or the attack gets clipped.
constexpr uint32_t kAmpSettleMs = 30;

bool ensureAudioRunning() {
  if (gAudioState == AUDIO_RUNNING) return true;

  if (!initI2s()) return false;
  ensureTaskStarted();

  // Let the amp settle while the task streams silence through DMA.
  delay(kAmpSettleMs);

  gAudioState = AUDIO_RUNNING;
  gIdleStartMs = 0;
  Serial.println("NS4168: audio activated");
  return true;
}

void shutdownAudio() {
  if (gAudioState == AUDIO_OFF) return;

  // Signal task to stop cooperatively
  gShutdownRequested = true;

  // Wait for task to finish current i2s_write and self-exit
  if (gTaskStarted && gAudioTask != nullptr) {
    uint32_t waitStart = millis();
    while (gTaskStarted && (millis() - waitStart < 100)) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    // Forceful delete as fallback
    if (gTaskStarted) {
      vTaskDelete(gAudioTask);
      gTaskStarted = false;
    }
    gAudioTask = nullptr;
  }

  // Tear down I2S - stopping BCLK puts the NS4168 into low-power mode
  if (gI2sReady) {
    i2s_zero_dma_buffer((i2s_port_t)AUDIO_I2S_PORT);
    i2s_driver_uninstall((i2s_port_t)AUDIO_I2S_PORT);
    gI2sReady = false;
  }

  // Reset tone state
  gCurrentFreq = 0;
  gPhaseStep = 0;
  gTargetGain = 0;
  gCurrentGain = 0;
  gIdleStartMs = 0;
  gShutdownRequested = false;

  gAudioState = AUDIO_OFF;
  Serial.println("NS4168: audio shutdown");
}

}  // namespace

// ----- public API -----

void buzzerBackendInit() {
  buildWaveTable();
  gCurrentFreq = 0;
  gPhaseStep = 0;
  gTargetGain = 0;
  gCurrentGain = 0;
  gLastTonePhaseStep = 0;
  gAudioState = AUDIO_OFF;
  // Do NOT start I2S/task here - lazy init on first sound
}

void buzzerBackendApplyStep(uint16_t freq) {
  if (freq > 0) {
    if (!ensureAudioRunning()) return;
    gIdleStartMs = 0;  // reset idle timer
    if (gPhaseStep == 0) {
      gPhase = 0;  // new tone starting - reset phase for clean waveform
    }
  }
  gCurrentFreq = freq;
  gPhaseStep = phaseStepFor(freq);
  if (gPhaseStep != 0) gLastTonePhaseStep = gPhaseStep;
  gTargetGain = (freq > 0) ? kEnvMax : 0;
}

void buzzerBackendStop() {
  gCurrentFreq = 0;
  gPhaseStep = 0;
  gTargetGain = 0;
  // Envelope decays naturally in audio task.
  // Idle timer will be armed by buzzerBackendTick() once gCurrentGain reaches 0.
}

void buzzerBackendTick() {
  if (gAudioState != AUDIO_RUNNING) return;

  // Check if audio pipeline is idle (silence + envelope fully decayed)
  if (gTargetGain == 0 && gCurrentGain == 0) {
    if (gIdleStartMs == 0) {
      gIdleStartMs = millis();
    } else if (millis() - gIdleStartMs >= kIdleTimeoutMs) {
      shutdownAudio();
    }
  } else {
    gIdleStartMs = 0;  // sound is playing or envelope ramping
  }
}

void buzzerBackendShutdown() {
  // Force immediate silence (skip envelope ramp)
  gCurrentGain = 0;
  gTargetGain = 0;
  gPhaseStep = 0;
  gCurrentFreq = 0;
  shutdownAudio();
}

#endif // BOARD_HAS_NS4168_AUDIO
