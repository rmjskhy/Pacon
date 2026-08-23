#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* PACON's first BLE control channel.  The UUIDs and PING/PONG behaviour are
 * intentionally identical to test_Pacon/ble_test so nRF Connect can be used
 * as a regression tool while the watch UI is being integrated. */
void ble_pacon_init(void);
bool ble_pacon_is_enabled(void);
bool ble_pacon_is_connected(void);
esp_err_t ble_pacon_set_enabled(bool enabled);

/* The standard HID-over-GATT consumer-control report lets a paired phone
 * treat PACON as a camera remote.  The implementation sends a short
 * Volume Increment press/release pulse; Android and iOS camera apps commonly
 * expose the volume key as their shutter action. */
bool ble_pacon_is_camera_remote_enabled(void);
esp_err_t ble_pacon_set_camera_remote_enabled(bool enabled);
esp_err_t ble_pacon_camera_shutter(void);

/* The application owns configuration and status state.  BLE only transports
 * a short, line-oriented command and returns the application response. */
typedef esp_err_t (*ble_pacon_command_handler_t)(const char *command,
                                                 char *response,
                                                 size_t response_size);
void ble_pacon_set_command_handler(ble_pacon_command_handler_t handler);

/* Optional binary write channel used for high-throughput media uploads.  The
 * callback receives one complete BLE write, including the protocol header,
 * and may return an occasional short notification in response. */
typedef esp_err_t (*ble_pacon_binary_handler_t)(const uint8_t *data,
                                                size_t data_size,
                                                char *response,
                                                size_t response_size);
void ble_pacon_set_binary_handler(ble_pacon_binary_handler_t handler);
