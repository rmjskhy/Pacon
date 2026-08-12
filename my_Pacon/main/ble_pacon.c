#include "ble_pacon.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "PACON_BLE";

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0x9a, 0x70, 0x43, 0x21, 0x5b, 0x8c, 0x4d, 0x2a,
    0x91, 0x50, 0x50, 0x41, 0x43, 0x4f, 0x4e, 0x01);
static const ble_uuid128_t s_status_uuid = BLE_UUID128_INIT(
    0x9a, 0x70, 0x43, 0x21, 0x5b, 0x8c, 0x4d, 0x2a,
    0x91, 0x50, 0x50, 0x41, 0x43, 0x4f, 0x4e, 0x02);
static const ble_uuid128_t s_command_uuid = BLE_UUID128_INIT(
    0x9a, 0x70, 0x43, 0x21, 0x5b, 0x8c, 0x4d, 0x2a,
    0x91, 0x50, 0x50, 0x41, 0x43, 0x4f, 0x4e, 0x03);
static const ble_uuid128_t s_response_uuid = BLE_UUID128_INIT(
    0x9a, 0x70, 0x43, 0x21, 0x5b, 0x8c, 0x4d, 0x2a,
    0x91, 0x50, 0x50, 0x41, 0x43, 0x4f, 0x4e, 0x04);
static const ble_uuid128_t s_media_data_uuid = BLE_UUID128_INIT(
    0x9a, 0x70, 0x43, 0x21, 0x5b, 0x8c, 0x4d, 0x2a,
    0x91, 0x50, 0x50, 0x41, 0x43, 0x4f, 0x4e, 0x05);

static uint16_t s_response_value_handle;
static uint16_t s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;
static bool s_enabled = true;
static bool s_synced;
static ble_pacon_command_handler_t s_command_handler;
static ble_pacon_binary_handler_t s_binary_handler;

/*
 * NimBLE invokes the GATT access callback on nimble_host.  Keep all sizeable
 * command/media buffers out of that task's stack; the callback is serialized
 * by the host and this device accepts one connection at a time.
 */
static uint8_t s_command_data[244];
static char s_command_response[280];
static uint8_t s_binary_data[512];
static char s_binary_response[96];

static void ble_start_advertising(void);

static int append_text(struct os_mbuf *om, const char *text)
{
    return os_mbuf_append(om, text, strlen(text)) == 0 ? 0 :
           BLE_ATT_ERR_INSUFFICIENT_RES;
}

static void notify_response(const char *response)
{
    if (response == NULL || response[0] == '\0' ||
        s_connection_handle == BLE_HS_CONN_HANDLE_NONE ||
        s_response_value_handle == 0U) {
        return;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(response, strlen(response));
    if (om == NULL) return;
    const int rc = ble_gatts_notify_custom(s_connection_handle,
                                           s_response_value_handle, om);
    if (rc != 0) os_mbuf_free_chain(om);
}

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    const uintptr_t kind = (uintptr_t)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && kind == 1U) {
        return append_text(ctxt->om, "PACON BLE ready; use nRF Connect or the phone app\r\n");
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && kind == 2U) {
        const uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
        const uint16_t copy_length = length < sizeof(s_command_data) - 1U ? length : sizeof(s_command_data) - 1U;
        if (os_mbuf_copydata(ctxt->om, 0, copy_length, s_command_data) != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        s_command_data[copy_length] = '\0';
        ESP_LOGI(TAG, "COMMAND conn=%u len=%u text=\"%s\"",
                 (unsigned)conn_handle, (unsigned)copy_length, (char *)s_command_data);
        memset(s_command_response, 0, sizeof(s_command_response));
        if (copy_length == 4U && memcmp(s_command_data, "PING", 4U) == 0) {
            snprintf(s_command_response, sizeof(s_command_response), "PONG\r\n");
        } else if (s_command_handler != NULL) {
            const esp_err_t command_err = s_command_handler((const char *)s_command_data,
                                                             s_command_response,
                                                             sizeof(s_command_response));
            if (command_err != ESP_OK && s_command_response[0] == '\0') {
                snprintf(s_command_response, sizeof(s_command_response), "ERR %s\r\n",
                         esp_err_to_name(command_err));
            }
        }
        if (s_command_response[0] == '\0') {
            snprintf(s_command_response, sizeof(s_command_response), "RX %u bytes: %s\r\n",
                     (unsigned)copy_length, (char *)s_command_data);
        }
        notify_response(s_command_response);
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && kind == 4U) {
        const uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
        if (length > sizeof(s_binary_data) ||
            os_mbuf_copydata(ctxt->om, 0, length, s_binary_data) != 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        memset(s_binary_response, 0, sizeof(s_binary_response));
        if (s_binary_handler == NULL) {
            snprintf(s_binary_response, sizeof(s_binary_response), "ERR binary media unavailable\r\n");
            notify_response(s_binary_response);
            return BLE_ATT_ERR_UNLIKELY;
        }
        const esp_err_t handler_err = s_binary_handler(s_binary_data, length,
                                                       s_binary_response, sizeof(s_binary_response));
        if (handler_err != ESP_OK && s_binary_response[0] == '\0') {
            snprintf(s_binary_response, sizeof(s_binary_response), "ERR %s\r\n",
                     esp_err_to_name(handler_err));
        }
        notify_response(s_binary_response);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_status_uuid.u, .access_cb = gatt_access_cb,
                .arg = (void *)(uintptr_t)1U, .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &s_command_uuid.u, .access_cb = gatt_access_cb,
                .arg = (void *)(uintptr_t)2U,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_response_uuid.u, .access_cb = gatt_access_cb,
                .arg = (void *)(uintptr_t)3U,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_response_value_handle,
            },
            {
                .uuid = &s_media_data_uuid.u, .access_cb = gatt_access_cb,
                .arg = (void *)(uintptr_t)4U,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 },
        },
    },
    { 0 },
};

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connection_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "CONNECTED conn=%u", (unsigned)s_connection_handle);
        } else if (s_enabled) {
            ble_start_advertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        if (s_enabled) ble_start_advertising();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_enabled) ble_start_advertising();
        return 0;
    default:
        return 0;
    }
}

static void ble_start_advertising(void)
{
    if (!s_synced || !s_enabled) return;
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)"PACON-BLE-TEST";
    fields.name_len = strlen("PACON-BLE-TEST");
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }
    struct ble_hs_adv_fields rsp = {0};
    rsp.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields rc=%d", rc);
        return;
    }
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, gap_event_cb, NULL);
    ESP_LOGI(TAG, "ADVERTISING name=PACON-BLE-TEST rc=%d", rc);
}

static void ble_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto rc=%d", rc);
        return;
    }
    uint8_t addr[6] = {0};
    if (ble_hs_id_copy_addr(s_own_addr_type, addr, NULL) == 0) {
        ESP_LOGI(TAG, "BLE address=%02X:%02X:%02X:%02X:%02X:%02X",
                 addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    }
    s_synced = true;
    ble_start_advertising();
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_pacon_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        (void)nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_INVALID_STATE) {
        ESP_LOGE(TAG, "NVS init rc=%s", esp_err_to_name(err));
        return;
    }
    ESP_ERROR_CHECK(nimble_port_init());
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("PACON-BLE-TEST");
    ble_hs_cfg.sync_cb = ble_on_sync;
    int rc = ble_gatts_count_cfg(s_services);
    ESP_ERROR_CHECK(rc == 0 ? ESP_OK : ESP_FAIL);
    rc = ble_gatts_add_svcs(s_services);
    ESP_ERROR_CHECK(rc == 0 ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "BLE ready; scan for PACON-BLE-TEST");
    nimble_port_freertos_init(ble_host_task);
}

bool ble_pacon_is_enabled(void) { return s_enabled; }
bool ble_pacon_is_connected(void) { return s_connection_handle != BLE_HS_CONN_HANDLE_NONE; }

void ble_pacon_set_command_handler(ble_pacon_command_handler_t handler)
{
    s_command_handler = handler;
}

void ble_pacon_set_binary_handler(ble_pacon_binary_handler_t handler)
{
    s_binary_handler = handler;
}

esp_err_t ble_pacon_set_enabled(bool enabled)
{
    s_enabled = enabled;
    if (!s_synced) return ESP_OK;
    if (!enabled) {
        if (s_connection_handle != BLE_HS_CONN_HANDLE_NONE) {
            (void)ble_gap_terminate(s_connection_handle,
                                     BLE_ERR_REM_USER_CONN_TERM);
        }
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        const int rc = ble_gap_adv_stop();
        return (rc == 0 || rc == BLE_HS_EALREADY) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    ble_start_advertising();
    return ESP_OK;
}
