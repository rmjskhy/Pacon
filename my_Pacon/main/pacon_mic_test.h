#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PACON_MIC_WAVEFORM_POINTS 64U
#define PACON_MIC_SAMPLE_RATE_HZ  16000U
#define PACON_MIC_MAX_RECORD_MS   60000U

typedef enum {
    PACON_MIC_OFF = 0,
    PACON_MIC_MONITORING,
    PACON_MIC_RECORDING,
    PACON_MIC_ERROR,
} pacon_mic_state_t;

typedef enum {
    PACON_MIC_VOICE_NONE = 0,
    PACON_MIC_VOICE_START,
    PACON_MIC_VOICE_STOP,
} pacon_mic_voice_command_t;

typedef struct {
    pacon_mic_state_t state;
    esp_err_t last_error;
    int16_t rms_dbfs_x10;
    int16_t peak_dbfs_x10;
    int16_t floor_dbfs_x10;
    uint32_t clipped_samples;
    uint32_t recorded_ms;
    bool signal_present;
    bool file_ready;
    bool voice_commands_enabled;
    bool voice_commands_ready;
    uint32_t voice_command_count;
    int16_t waveform[PACON_MIC_WAVEFORM_POINTS];
} pacon_mic_status_t;

/* Open starts continuous monitoring. The module owns I2S until close. */
esp_err_t pacon_mic_open(void);
/* Recording is mono 16-bit PCM WAV and automatically stops after 60 seconds. */
esp_err_t pacon_mic_start_recording(const char *path);
esp_err_t pacon_mic_stop_recording(void);
/* MultiNet listens for "wo cao" to toggle recording only while Recorder is open. */
esp_err_t pacon_mic_set_voice_commands_enabled(bool enabled);
/* Returns each recognized action once. */
pacon_mic_voice_command_t pacon_mic_take_voice_command(void);
void pacon_mic_get_status(pacon_mic_status_t *status);
/* Close always finalizes an active WAV before releasing I2S. */
void pacon_mic_close(void);

#ifdef __cplusplus
}
#endif
