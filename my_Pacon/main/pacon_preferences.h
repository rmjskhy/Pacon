#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* User choices only: no animation, connection handles or USB ownership. */
typedef struct {
    uint8_t shape;
    bool custom;
    uint16_t hue;        /* 0..9999, one turn = 10000 */
    uint16_t saturation; /* 1500..10000 */
} pacon_fluid_preferences_t;

typedef struct {
    bool automatic;
    bool tilt;
    uint8_t mood;
} pacon_ouo_preferences_t;

void pacon_load_fluid_preferences(pacon_fluid_preferences_t *value);
esp_err_t pacon_save_fluid_preferences(const pacon_fluid_preferences_t *value);
void pacon_load_ouo_preferences(pacon_ouo_preferences_t *value);
esp_err_t pacon_save_ouo_preferences(const pacon_ouo_preferences_t *value);
bool pacon_load_switch(const char *key, bool fallback);
esp_err_t pacon_save_switch(const char *key, bool value);
