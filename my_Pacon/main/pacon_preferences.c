#include "pacon_preferences.h"
#include <stddef.h>
#include <string.h>
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "PACON_PREFS";
#define PREFS_NAMESPACE "pacon_prefs"

static bool read_record(const char *key, uint8_t *bytes, size_t expected)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PREFS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return false;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Read %s: %s", key, esp_err_to_name(err));
        return false;
    }
    size_t size = expected;
    err = nvs_get_blob(handle, key, bytes, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != expected) return false;
    return true;
}

/* One versioned record per feature: a torn update cannot combine a new shape
 * with half an old colour. Equal choices do not consume another flash write. */
static esp_err_t write_record(const char *key, const uint8_t *bytes, size_t size)
{
    uint8_t previous[8];
    if (size > sizeof(previous)) return ESP_ERR_INVALID_SIZE;
    if (read_record(key, previous, size) && memcmp(previous, bytes, size) == 0)
        return ESP_OK;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PREFS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, key, bytes, size);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "Save %s failed: %s", key, esp_err_to_name(err));
    return err;
}

void pacon_load_fluid_preferences(pacon_fluid_preferences_t *value)
{
    uint8_t data[7];
    if (!read_record("fluid", data, sizeof(data))) return;
    const uint16_t hue = ((uint16_t)data[3] << 8) | data[4];
    const uint16_t saturation = ((uint16_t)data[5] << 8) | data[6];
    if (data[0] != 1 || data[1] > 2 || data[2] > 1 || hue > 9999 ||
        saturation < 1500 || saturation > 10000) return;
    value->shape = data[1];
    value->custom = data[2] != 0;
    value->hue = hue;
    value->saturation = saturation;
}

esp_err_t pacon_save_fluid_preferences(const pacon_fluid_preferences_t *value)
{
    if (value->shape > 2 || value->hue > 9999 || value->saturation < 1500 ||
        value->saturation > 10000) return ESP_ERR_INVALID_ARG;
    const uint8_t data[7] = {1, value->shape, value->custom ? 1 : 0,
        value->hue >> 8, value->hue & 255,
        value->saturation >> 8, value->saturation & 255};
    return write_record("fluid", data, sizeof(data));
}

void pacon_load_ouo_preferences(pacon_ouo_preferences_t *value)
{
    uint8_t data[4];
    if (!read_record("ouo", data, sizeof(data))) return;
    if (data[0] != 1 || data[1] > 1 || data[2] > 1 || data[3] > 100) return;
    value->automatic = data[1] != 0;
    value->tilt = data[2] != 0;
    value->mood = data[3];
}

esp_err_t pacon_save_ouo_preferences(const pacon_ouo_preferences_t *value)
{
    if (value->mood > 100) return ESP_ERR_INVALID_ARG;
    const uint8_t data[4] = {1, value->automatic ? 1 : 0,
        value->tilt ? 1 : 0, value->mood};
    return write_record("ouo", data, sizeof(data));
}

bool pacon_load_switch(const char *key, bool fallback)
{
    uint8_t data[2];
    if (!read_record(key, data, sizeof(data)) || data[0] != 1 || data[1] > 1)
        return fallback;
    return data[1] != 0;
}

esp_err_t pacon_save_switch(const char *key, bool value)
{
    const uint8_t data[2] = {1, value ? 1 : 0};
    return write_record(key, data, sizeof(data));
}
