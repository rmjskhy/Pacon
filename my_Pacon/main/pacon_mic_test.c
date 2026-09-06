#include "pacon_mic_test.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define MIC_I2S_PORT       I2S_NUM_0
#define MIC_PIN_BCLK       GPIO_NUM_39
#define MIC_PIN_WS         GPIO_NUM_40
#define MIC_PIN_DATA       GPIO_NUM_47
#define MIC_FRAMES_BLOCK     256U
#define MIC_TASK_STACK_BYTES 6144U
#define MIC_DIGITAL_GAIN_X  4
#define MIC_LIMITER_KNEE    24576
#define MIC_LIMITER_RANGE   (INT16_MAX - MIC_LIMITER_KNEE)
#define MIC_BYTES_PER_SEC  (PACON_MIC_SAMPLE_RATE_HZ * sizeof(int16_t))
#define MIC_MAX_DATA_BYTES ((PACON_MIC_MAX_RECORD_MS * MIC_BYTES_PER_SEC) / 1000U)
#define MIC_PATH_MAX       160U
#define MIC_COMMAND_WINDOW_MS 6000
#define MIC_COMMAND_START_ID  1

static const char *TAG = "PACON_MIC";
static i2s_chan_handle_t s_rx_channel;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_task_done;
static TaskHandle_t s_task;
static volatile bool s_running;
static FILE *s_record_file;
static char s_record_final_path[MIC_PATH_MAX];
static char s_record_temp_path[MIC_PATH_MAX];
static uint32_t s_record_bytes;
static srmodel_list_t *s_sr_models;
static esp_mn_iface_t *s_multinet;
static model_iface_data_t *s_multinet_data;
static int16_t *s_multinet_pcm;
static size_t s_multinet_chunk_samples;
static size_t s_multinet_fill;
static bool s_multinet_commands_allocated;
static pacon_mic_voice_command_t s_voice_command_event;
static esp_pm_lock_handle_t s_wake_cpu_lock;
static bool s_wake_cpu_lock_acquired;
static pacon_mic_status_t s_status = {
    .state = PACON_MIC_OFF,
    .last_error = ESP_OK,
    .rms_dbfs_x10 = -960,
    .peak_dbfs_x10 = -960,
    .floor_dbfs_x10 = -960,
};

static void put_le16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static bool write_wav_header(FILE *file, uint32_t data_bytes)
{
    uint8_t header[44] = {0};
    memcpy(header + 0, "RIFF", 4);
    put_le32(header + 4, 36U + data_bytes);
    memcpy(header + 8, "WAVEfmt ", 8);
    put_le32(header + 16, 16U);
    put_le16(header + 20, 1U);
    put_le16(header + 22, 1U);
    put_le32(header + 24, PACON_MIC_SAMPLE_RATE_HZ);
    put_le32(header + 28, MIC_BYTES_PER_SEC);
    put_le16(header + 32, sizeof(int16_t));
    put_le16(header + 34, 16U);
    memcpy(header + 36, "data", 4);
    put_le32(header + 40, data_bytes);
    if (fseek(file, 0, SEEK_SET) != 0) return false;
    return fwrite(header, 1, sizeof(header), file) == sizeof(header) && fflush(file) == 0;
}

static int16_t amplitude_dbfs_x10(double amplitude)
{
    if (amplitude < 1.0) return -960;
    const double db_x10 = 200.0 * log10(amplitude / 32768.0);
    if (db_x10 < -960.0) return -960;
    if (db_x10 > 0.0) return 0;
    return (int16_t)lround(db_x10);
}

static int16_t apply_soft_limited_gain(int32_t sample)
{
    const int32_t amplified = sample * MIC_DIGITAL_GAIN_X;
    const bool negative = amplified < 0;
    const int32_t magnitude = negative ? -amplified : amplified;
    if (magnitude <= MIC_LIMITER_KNEE) return (int16_t)amplified;

    const int32_t excess = magnitude - MIC_LIMITER_KNEE;
    const int32_t compressed = MIC_LIMITER_KNEE +
        (int32_t)(((int64_t)excess * MIC_LIMITER_RANGE) /
                  (excess + MIC_LIMITER_RANGE));
    return (int16_t)(negative ? -compressed : compressed);
}

/* s_lock must be held. MultiNet weights use the model partition/PSRAM; the
 * command engine exists only while its Recorder-page switch is enabled. */
static void voice_commands_disable_locked(void)
{
    if (s_wake_cpu_lock_acquired) {
        esp_pm_lock_release(s_wake_cpu_lock);
        s_wake_cpu_lock_acquired = false;
    }
    if (s_multinet != NULL && s_multinet_data != NULL) {
        s_multinet->destroy(s_multinet_data);
    }
    s_multinet_data = NULL;
    s_multinet = NULL;
    if (s_multinet_commands_allocated) (void)esp_mn_commands_free();
    s_multinet_commands_allocated = false;
    free(s_multinet_pcm);
    s_multinet_pcm = NULL;
    s_multinet_chunk_samples = 0;
    s_multinet_fill = 0;
    if (s_sr_models != NULL) esp_srmodel_deinit(s_sr_models);
    s_sr_models = NULL;
    s_voice_command_event = PACON_MIC_VOICE_NONE;
    s_status.voice_commands_enabled = false;
    s_status.voice_commands_ready = false;
}

/* s_lock must be held. */
static esp_err_t voice_commands_enable_locked(void)
{
    if (s_status.voice_commands_ready) return ESP_OK;
    s_status.voice_commands_enabled = true;
    s_status.voice_commands_ready = false;
    s_sr_models = esp_srmodel_init("model");
    if (s_sr_models == NULL) return ESP_ERR_NOT_FOUND;
    char *model_name = esp_srmodel_filter(s_sr_models, ESP_MN_PREFIX,
                                          ESP_MN_CHINESE);
    if (model_name == NULL) {
        voice_commands_disable_locked();
        s_status.voice_commands_enabled = true;
        return ESP_ERR_NOT_FOUND;
    }
    s_multinet = esp_mn_handle_from_name(model_name);
    if (s_multinet == NULL) {
        voice_commands_disable_locked();
        s_status.voice_commands_enabled = true;
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_multinet_data = s_multinet->create(model_name, MIC_COMMAND_WINDOW_MS);
    if (s_multinet_data == NULL) {
        voice_commands_disable_locked();
        s_status.voice_commands_enabled = true;
        return ESP_ERR_NO_MEM;
    }
    const int chunk = s_multinet->get_samp_chunksize(s_multinet_data);
    if (chunk <= 0 || s_multinet->get_samp_rate(s_multinet_data) !=
                      (int)PACON_MIC_SAMPLE_RATE_HZ) {
        voice_commands_disable_locked();
        s_status.voice_commands_enabled = true;
        return ESP_ERR_INVALID_SIZE;
    }
    if (esp_mn_commands_alloc(s_multinet, s_multinet_data) != ESP_OK) {
        voice_commands_disable_locked();
        s_status.voice_commands_enabled = true;
        return ESP_ERR_NO_MEM;
    }
    s_multinet_commands_allocated = true;
    if (esp_mn_commands_add(MIC_COMMAND_START_ID, "wo cao") != ESP_OK ||
        esp_mn_commands_update() != NULL) {
        voice_commands_disable_locked();
        s_status.voice_commands_enabled = true;
        return ESP_ERR_INVALID_ARG;
    }
    s_multinet_pcm = heap_caps_malloc((size_t)chunk * sizeof(int16_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_multinet_pcm == NULL) {
        voice_commands_disable_locked();
        s_status.voice_commands_enabled = true;
        return ESP_ERR_NO_MEM;
    }
    s_multinet_chunk_samples = (size_t)chunk;
    s_multinet_fill = 0;
    if (s_wake_cpu_lock == NULL &&
        esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "pacon_wake",
                           &s_wake_cpu_lock) != ESP_OK) {
        voice_commands_disable_locked();
        s_status.voice_commands_enabled = true;
        return ESP_ERR_NO_MEM;
    }
    if (esp_pm_lock_acquire(s_wake_cpu_lock) != ESP_OK) {
        voice_commands_disable_locked();
        s_status.voice_commands_enabled = true;
        return ESP_FAIL;
    }
    s_wake_cpu_lock_acquired = true;
    s_status.voice_commands_ready = true;
    s_status.last_error = ESP_OK;
    ESP_LOGI(TAG, "MultiNet ready: %s; wo cao toggles recording", model_name);
    return ESP_OK;
}

/* s_lock must be held. */
static esp_err_t finalize_recording_locked(bool ready)
{
    if (s_record_file == NULL) return ESP_OK;
    const bool header_ok = write_wav_header(s_record_file, s_record_bytes);
    const bool close_ok = fclose(s_record_file) == 0;
    s_record_file = NULL;
    bool commit_ok = ready && header_ok && close_ok && s_record_bytes > 0;
    if (commit_ok) {
        errno = 0;
        if (remove(s_record_final_path) != 0 && errno != ENOENT) {
            commit_ok = false;
        } else if (rename(s_record_temp_path, s_record_final_path) != 0) {
            commit_ok = false;
        }
    }
    if (!commit_ok) {
        (void)remove(s_record_temp_path);
    }
    s_status.recorded_ms = (uint32_t)(((uint64_t)s_record_bytes * 1000U) /
                                      MIC_BYTES_PER_SEC);
    s_status.file_ready = commit_ok;
    if (!commit_ok) {
        if (s_status.last_error == ESP_OK) s_status.last_error = ESP_FAIL;
        s_status.state = PACON_MIC_ERROR;
        ESP_LOGE(TAG, "WAV finalization failed after %lu bytes",
                 (unsigned long)s_record_bytes);
        return s_status.last_error;
    }
    s_status.state = PACON_MIC_MONITORING;
    ESP_LOGI(TAG, "WAV saved atomically: %lu bytes, %lu ms",
             (unsigned long)s_record_bytes,
             (unsigned long)s_status.recorded_ms);
    return ESP_OK;
}

static void mic_capture_task(void *context)
{
    (void)context;
    static int32_t raw[MIC_FRAMES_BLOCK * 2U];
    static int16_t pcm[MIC_FRAMES_BLOCK];

    while (s_running) {
        size_t bytes_read = 0;
        const esp_err_t err = i2s_channel_read(s_rx_channel, raw, sizeof(raw),
                                                &bytes_read, 100);
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_status.last_error = err;
            s_status.state = PACON_MIC_ERROR;
            xSemaphoreGive(s_lock);
            ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));
            break;
        }

        const size_t slots = bytes_read / sizeof(raw[0]);
        const size_t frames = slots / 2U;
        if (frames == 0) continue;
        int64_t square_sum = 0;
        int32_t peak = 0;
        uint32_t clipped = 0;
        for (size_t frame = 0; frame < frames; ++frame) {
            /* The microphone sends a 24-bit left-channel word, MSB-aligned
             * in the 32-bit I2S slot. Convert to signed 16-bit PCM. */
            int32_t sample = raw[frame * 2U] >> 16;
            sample = apply_soft_limited_gain(sample);
            if (sample > INT16_MAX) sample = INT16_MAX;
            if (sample < INT16_MIN) sample = INT16_MIN;
            pcm[frame] = (int16_t)sample;
            const int32_t magnitude = sample < 0 ? -sample : sample;
            if (magnitude > peak) peak = magnitude;
            if (magnitude >= 32700) ++clipped;
            square_sum += (int64_t)sample * sample;
        }
        const double rms = sqrt((double)square_sum / (double)frames);
        const int16_t rms_db = amplitude_dbfs_x10(rms);
        const int16_t peak_db = amplitude_dbfs_x10((double)peak);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_status.state != PACON_MIC_ERROR) {
            s_status.rms_dbfs_x10 = rms_db;
            s_status.peak_dbfs_x10 = peak_db;
            if (s_status.floor_dbfs_x10 == -960 || rms_db < s_status.floor_dbfs_x10) {
                s_status.floor_dbfs_x10 = rms_db;
            }
            s_status.signal_present = peak_db > -750;
            s_status.clipped_samples += clipped;
            for (size_t point = 0; point < PACON_MIC_WAVEFORM_POINTS; ++point) {
                size_t frame = point * frames / PACON_MIC_WAVEFORM_POINTS;
                if (frame >= frames) frame = frames - 1U;
                s_status.waveform[point] = pcm[frame];
            }
        }

        if (s_record_file != NULL) {
            const size_t written = fwrite(pcm, sizeof(pcm[0]), frames, s_record_file);
            s_record_bytes += (uint32_t)(written * sizeof(pcm[0]));
            s_status.recorded_ms = (uint32_t)(((uint64_t)s_record_bytes * 1000U) /
                                              MIC_BYTES_PER_SEC);
            if (written != frames) {
                s_status.last_error = ESP_FAIL;
                s_status.state = PACON_MIC_ERROR;
                (void)finalize_recording_locked(false);
            } else if (s_record_bytes >= MIC_MAX_DATA_BYTES) {
                (void)finalize_recording_locked(true);
            }
        }
        if (s_status.voice_commands_ready && s_multinet_pcm != NULL) {
            size_t consumed = 0;
            while (consumed < frames) {
                const size_t room = s_multinet_chunk_samples - s_multinet_fill;
                const size_t count = (frames - consumed) < room ?
                                     (frames - consumed) : room;
                memcpy(s_multinet_pcm + s_multinet_fill, pcm + consumed,
                       count * sizeof(pcm[0]));
                s_multinet_fill += count;
                consumed += count;
                if (s_multinet_fill == s_multinet_chunk_samples) {
                    const esp_mn_state_t state =
                        s_multinet->detect(s_multinet_data, s_multinet_pcm);
                    s_multinet_fill = 0;
                    if (state == ESP_MN_STATE_DETECTED) {
                        esp_mn_results_t *results =
                            s_multinet->get_results(s_multinet_data);
                        const int command_id = results != NULL && results->num > 0 ?
                                               results->command_id[0] : 0;
                        if (s_voice_command_event == PACON_MIC_VOICE_NONE &&
                            command_id == MIC_COMMAND_START_ID) {
                            if (s_status.state == PACON_MIC_MONITORING) {
                                s_voice_command_event = PACON_MIC_VOICE_START;
                            } else if (s_status.state == PACON_MIC_RECORDING) {
                                s_voice_command_event = PACON_MIC_VOICE_STOP;
                            }
                        }
                        ++s_status.voice_command_count;
                        ESP_LOGI(TAG, "voice command=%d text=%s probability=%.3f",
                                 command_id,
                                 results != NULL ? results->string : "?",
                                 results != NULL && results->num > 0 ?
                                 results->prob[0] : 0.0f);
                    } else if (state == ESP_MN_STATE_TIMEOUT) {
                        s_multinet->clean(s_multinet_data);
                    }
                }
            }
        }
        xSemaphoreGive(s_lock);
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_record_file != NULL) (void)finalize_recording_locked(true);
    xSemaphoreGive(s_lock);
    (void)i2s_channel_disable(s_rx_channel);
    s_running = false;
    s_task = NULL;
    xSemaphoreGive(s_task_done);
    vTaskDelete(NULL);
}

esp_err_t pacon_mic_open(void)
{
    if (s_running) return ESP_OK;
    if (s_rx_channel != NULL || s_task_done != NULL) pacon_mic_close();
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    s_task_done = xSemaphoreCreateBinary();
    if (s_task_done == NULL) return ESP_ERR_NO_MEM;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = PACON_MIC_MONITORING;
    s_status.last_error = ESP_OK;
    s_status.rms_dbfs_x10 = -960;
    s_status.peak_dbfs_x10 = -960;
    s_status.floor_dbfs_x10 = -960;
    s_record_bytes = 0;
    xSemaphoreGive(s_lock);

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_PORT,
                                                           I2S_ROLE_MASTER);
    channel.dma_desc_num = 6;
    channel.dma_frame_num = MIC_FRAMES_BLOCK;
    esp_err_t err = i2s_new_channel(&channel, NULL, &s_rx_channel);
    if (err != ESP_OK) goto fail;

    i2s_std_config_t standard = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PACON_MIC_SAMPLE_RATE_HZ),
        /* Stereo framing is required even for one microphone: the part needs
         * 64 SCK cycles per WS frame (32 clocks for each channel). */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_PIN_BCLK,
            .ws = MIC_PIN_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_PIN_DATA,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    standard.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    err = i2s_channel_init_std_mode(s_rx_channel, &standard);
    if (err != ESP_OK) goto fail_channel;
    err = i2s_channel_enable(s_rx_channel);
    if (err != ESP_OK) goto fail_channel;

    s_running = true;
    if (xTaskCreate(mic_capture_task, "pacon_mic", MIC_TASK_STACK_BYTES,
                    NULL, 5, &s_task) != pdPASS) {
        s_running = false;
        (void)i2s_channel_disable(s_rx_channel);
        err = ESP_ERR_NO_MEM;
        goto fail_channel;
    }
    ESP_LOGI(TAG, "monitoring at %u Hz, Philips I2S 24-in-32 left channel",
             (unsigned)PACON_MIC_SAMPLE_RATE_HZ);
    return ESP_OK;

fail_channel:
    (void)i2s_del_channel(s_rx_channel);
    s_rx_channel = NULL;
fail:
    vSemaphoreDelete(s_task_done);
    s_task_done = NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = PACON_MIC_ERROR;
    s_status.last_error = err;
    xSemaphoreGive(s_lock);
    ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t pacon_mic_start_recording(const char *path)
{
    if (!s_running || path == NULL || path[0] == '\0') return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_record_file != NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const int final_length = snprintf(s_record_final_path, sizeof(s_record_final_path),
                                      "%s", path);
    const int temp_length = snprintf(s_record_temp_path, sizeof(s_record_temp_path),
                                     "%s.tmp", path);
    if (final_length < 0 || final_length >= (int)sizeof(s_record_final_path) ||
        temp_length < 0 || temp_length >= (int)sizeof(s_record_temp_path)) {
        s_status.last_error = ESP_ERR_INVALID_SIZE;
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_SIZE;
    }
    (void)remove(s_record_temp_path);
    s_record_file = fopen(s_record_temp_path, "wb+");
    if (s_record_file == NULL) {
        s_status.last_error = ESP_FAIL;
        xSemaphoreGive(s_lock);
        ESP_LOGE(TAG, "cannot create %s", s_record_temp_path);
        return ESP_FAIL;
    }
    s_record_bytes = 0;
    s_status.recorded_ms = 0;
    s_status.file_ready = false;
    s_status.last_error = ESP_OK;
    if (!write_wav_header(s_record_file, 0)) {
        fclose(s_record_file);
        s_record_file = NULL;
        (void)remove(s_record_temp_path);
        s_status.last_error = ESP_FAIL;
        xSemaphoreGive(s_lock);
        return ESP_FAIL;
    }
    if (fseek(s_record_file, 44, SEEK_SET) != 0) {
        fclose(s_record_file);
        s_record_file = NULL;
        (void)remove(s_record_temp_path);
        s_status.last_error = ESP_FAIL;
        xSemaphoreGive(s_lock);
        return ESP_FAIL;
    }
    s_status.state = PACON_MIC_RECORDING;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "recording to %s (maximum %u ms)", s_record_temp_path,
             (unsigned)PACON_MIC_MAX_RECORD_MS);
    return ESP_OK;
}

esp_err_t pacon_mic_stop_recording(void)
{
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const esp_err_t err = finalize_recording_locked(true);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t pacon_mic_set_voice_commands_enabled(bool enabled)
{
    if (s_lock == NULL || !s_running) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (enabled) {
        err = voice_commands_enable_locked();
        if (err != ESP_OK) s_status.last_error = err;
    } else {
        voice_commands_disable_locked();
        s_status.last_error = ESP_OK;
    }
    xSemaphoreGive(s_lock);
    return err;
}

pacon_mic_voice_command_t pacon_mic_take_voice_command(void)
{
    if (s_lock == NULL || xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        return PACON_MIC_VOICE_NONE;
    }
    const pacon_mic_voice_command_t command = s_voice_command_event;
    s_voice_command_event = PACON_MIC_VOICE_NONE;
    xSemaphoreGive(s_lock);
    return command;
}

void pacon_mic_get_status(pacon_mic_status_t *status)
{
    if (status == NULL) return;
    if (s_lock == NULL || xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        memset(status, 0, sizeof(*status));
        status->state = PACON_MIC_OFF;
        status->last_error = ESP_ERR_INVALID_STATE;
        status->rms_dbfs_x10 = -960;
        status->peak_dbfs_x10 = -960;
        status->floor_dbfs_x10 = -960;
        return;
    }
    *status = s_status;
    xSemaphoreGive(s_lock);
}

void pacon_mic_close(void)
{
    if (s_rx_channel == NULL && s_task_done == NULL) return;
    s_running = false;
    if (s_task_done != NULL && xSemaphoreTake(s_task_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "capture task did not stop in time; preserving I2S resources");
        return;
    }
    if (s_rx_channel != NULL) {
        (void)i2s_del_channel(s_rx_channel);
        s_rx_channel = NULL;
    }
    if (s_task_done != NULL) {
        vSemaphoreDelete(s_task_done);
        s_task_done = NULL;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    voice_commands_disable_locked();
    s_status.state = PACON_MIC_OFF;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "monitor stopped and I2S released");
}
