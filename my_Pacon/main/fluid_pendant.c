/*
 * PACON fluid pendant
 *
 * A round, direct-QSPI liquid pendant for the SH8601 display.  The QMI8658
 * tilt vector moves a cluster of collision-resolved droplets; a touch creates
 * a radial impulse and cycles the liquid colour.
 */

#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "esp_private/usb_phy.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "ble_pacon.h"
#include "pacon_preferences.h"
#include "pacon_mic_test.h"
#include "ouo_idle_reference.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_lcd_sh8601.h"
#include "lvgl.h"
#include "cJSON.h"

static const char *TAG = "FLUID_PENDANT";

/* Verified PACON board connections. */
#define PIN_I2C_SCL             GPIO_NUM_1
#define PIN_I2C_SDA             GPIO_NUM_2
#define PIN_TOUCH_RST           GPIO_NUM_4

#define PIN_LCD_RST             GPIO_NUM_5
#define PIN_LCD_POWER           GPIO_NUM_6
#define PIN_LCD_CS              GPIO_NUM_7
#define PIN_LCD_CLK             GPIO_NUM_8
#define PIN_LCD_D3              GPIO_NUM_9
#define PIN_LCD_D2              GPIO_NUM_10
#define PIN_LCD_D1              GPIO_NUM_11
#define PIN_LCD_D0              GPIO_NUM_12

/* U2 MKDV4GCL-AB SD NAND, traced from the supplied PACON EDA project.
 * These are SDMMC signals, not the display's QSPI signals above. */
#define PIN_SD_D2               GPIO_NUM_14
#define PIN_SD_D3               GPIO_NUM_15
#define PIN_SD_CLK              GPIO_NUM_16
#define PIN_SD_CMD              GPIO_NUM_17
#define PIN_SD_D0               GPIO_NUM_18
#define PIN_SD_D1               GPIO_NUM_21
#define SD_NAND_MOUNT_POINT     "/sdnand"
#define MIC_TEST_WAV_PATH       SD_NAND_MOUNT_POINT "/MIC_TEST.WAV"
#define USB_MSC_BOOT_MAGIC      UINT32_C(0x554D5343) /* "UMSC" */

#define I2C_PORT                I2C_NUM_0
#define I2C_TIMEOUT_MS          50
#define ADDR_AXP2101            0x34
#define ADDR_TOUCH              0x38
#define ADDR_PCF85063           0x51
#define ADDR_QMI8658            0x6A
#define PIN_SPKOUT              GPIO_NUM_48

#define AXP2101_CHIP_ID         0x4A
#define AXP2101_CHIP_ID2        0x47
#define PMIC_STATUS_PERIOD_MS   5000

#define LCD_HOST                SPI2_HOST
#define LCD_WIDTH               475
#define LCD_HEIGHT              466

/* KKD1 uses the character's raised left arm and outstretched right arm as
 * stationary pointers.  The minute and hour dials rotate behind the artwork;
 * they are not conventional hands drawn over the character. */
#define WATCH_SOURCE_SIZE              450
#define WATCH_MINUTE_POINTER_ANGLE     0.0f
#define WATCH_HOUR_POINTER_ANGLE       (3.14159265f / 2.0f)

extern const uint8_t kkd1_character_start[]
    asm("_binary_kkd1_character_rgb565a_start");
extern const uint8_t kkd1_character_end[]
    asm("_binary_kkd1_character_rgb565a_end");
extern const uint8_t kkd1_second_dial_start[]
    asm("_binary_kkd1_second_dial_rgb565a_start");
extern const uint8_t kkd1_second_dial_end[]
    asm("_binary_kkd1_second_dial_rgb565a_end");
extern const uint8_t kkd1_minute_dial_start[]
    asm("_binary_kkd1_minute_dial_rgb565a_start");
extern const uint8_t kkd1_minute_dial_end[]
    asm("_binary_kkd1_minute_dial_rgb565a_end");
extern const uint8_t kkd1_hour_dial_start[]
    asm("_binary_kkd1_hour_dial_rgb565a_start");
extern const uint8_t kkd1_hour_dial_end[]
    asm("_binary_kkd1_hour_dial_rgb565a_end");
extern const uint8_t kkd1_mechanism_start[]
    asm("_binary_kkd1_mechanism_rgb565a_start");
extern const uint8_t kkd1_mechanism_end[]
    asm("_binary_kkd1_mechanism_rgb565a_end");
extern const uint8_t kkd1_complication_start[]
    asm("_binary_kkd1_complication_rgb565a_start");
extern const uint8_t kkd1_complication_end[]
    asm("_binary_kkd1_complication_rgb565a_end");
extern const uint8_t kkd2_background_start[]
    asm("_binary_kkd2_background_rgb565a_start");
extern const uint8_t kkd2_background_end[]
    asm("_binary_kkd2_background_rgb565a_end");
extern const uint8_t kkd2_hour_start[]
    asm("_binary_kkd2_hour_rgb565a_start");
extern const uint8_t kkd2_hour_end[]
    asm("_binary_kkd2_hour_rgb565a_end");
extern const uint8_t kkd2_minute_start[]
    asm("_binary_kkd2_minute_rgb565a_start");
extern const uint8_t kkd2_minute_end[]
    asm("_binary_kkd2_minute_rgb565a_end");
extern const uint8_t kkd2_complication_start[]
    asm("_binary_kkd2_complication_rgb565a_start");
extern const uint8_t kkd2_complication_end[]
    asm("_binary_kkd2_complication_rgb565a_end");

/* OuO 1.611 capture geometry normalized around the 2400x1080 screen centre
 * into the 466px round face: open eyes are about 30px radius, 226px apart,
 * and sit at y=216.  Keeping these values in sync with ouo-preview avoids
 * the previous crowded 44px-eye rendering. */
#define OUO_LEFT_EYE_X          120
#define OUO_RIGHT_EYE_X         346
#define OUO_FACE_CENTER_X       ((OUO_LEFT_EYE_X + OUO_RIGHT_EYE_X) / 2)
#define OUO_EYE_Y               216
#define OUO_MOUTH_Y             264
#define OUO_EYE_RADIUS          30
#define OUO_MENU_HOLD_MS        560
#define OUO_MENU_HOLD_TOLERANCE 30
/* Feature-local settings entrances share one reachable upper-right region. */
#define FEATURE_SETTINGS_X1     326
#define FEATURE_SETTINGS_X2     448
#define FEATURE_SETTINGS_Y1     24
#define FEATURE_SETTINGS_Y2     132
#define LCD_STRIPE_LINES        64
#define LCD_STRIPE_BYTES        (LCD_WIDTH * LCD_STRIPE_LINES * sizeof(uint16_t))
#define LCD_FRAME_PIXELS        (LCD_WIDTH * LCD_HEIGHT)
#define LCD_FRAME_BYTES         (LCD_FRAME_PIXELS * sizeof(uint16_t))
#define LCD_BRIGHTNESS_NORMAL   0x80U /* About 50%; avoids continuous OLED overdrive. */
#define LCD_BRIGHTNESS_DIM      0x24U /* Gentle idle level before the panel is blanked. */
#define DISPLAY_DIM_TIMEOUT_MS  20000
#define DISPLAY_SLEEP_TIMEOUT_DEFAULT_SECONDS 60U
#define HOME_PIXEL_SHIFT_MS     120000
#define HOME_STATUS_SHIFT_MS    60000
#define HOME_FALLBACK_MEDIA_COUNT 1
#define HOME_EXTERNAL_MEDIA_MAX 12
#define HOME_MEDIA_PATH_MAX     128
#define HOME_MEDIA_NAME_MAX     64
#define BLE_MEDIA_TEMP_NAME     "PACON.UPL" /* FAT 8.3-compatible staging file */
#define BLE_MEDIA_BINARY_HEADER 4U       /* little-endian sequential offset */
#define BLE_MEDIA_BINARY_ACK_EVERY 8U    /* one cumulative ACK per window */
#define HOME_MEDIA_INVALID_INDEX UINT8_MAX
#define HOME_MEDIA_CACHE_SLOTS  4
#define HOME_SLIDE_STEPS        4
#define APPS_DISMISS_STEPS      5
#define APPS_ENTRANCE_STEPS     4
#define FRAME_PERIOD_MS         30
#define OUO_FRAME_PERIOD_MS     16
#define HOME_REFRESH_MS         1000
#define FLUID_PERF_REPORT_MS    2000
#define SETTINGS_SCROLL_MAX      440
#define SETTINGS_BRIGHTNESS_MIN  0x20U
#define SETTINGS_BRIGHTNESS_MAX  0xD0U

/* SkyOrb / Plane Radar.  The original dual-display project is MIT licensed;
 * PACON uses a single 475x466 SH8601 panel and its own direct-QSPI path. */
#define SKYORB_FRAME_PERIOD_MS   180
/* Airplanes.live's public/free API allowance is about 500 requests/day.
 * Three minutes is at most 480 periodic requests/day if SkyOrb is left online
 * continuously; the first fetch after connecting still happens immediately. */
#define SKYORB_FETCH_PERIOD_MS   180000
#define SKYORB_FETCH_RETRY_MS    15000
#define SKYORB_MAX_AIRCRAFT      28
#define SKYORB_CONFIG_NAMESPACE  "skyorb"
#define CLOCK_CONFIG_NAMESPACE   "pacon_clock"
#define ALARM_RING_MS            20000

/* Ported from Opal_Fluid's "simple" mode.  PACON draws the droplets directly
 * into its SH8601 QSPI stripes, so the small round highlights remain sharp
 * rather than being enlarged from a coarse density field. */
#define PARTICLE_COUNT          220
#define PARTICLE_RADIUS         6
#define PARTICLE_DRAW_RADIUS    7
#define PARTICLE_MIN_DISTANCE   (PARTICLE_DRAW_RADIUS * 2 + 4)
#define PARTICLE_INIT_TOP       34
#define PARTICLE_MAX_SPEED_Q8   5750
#define HASH_CELL_SIZE          PARTICLE_MIN_DISTANCE
#define HASH_COLUMNS            ((LCD_WIDTH + HASH_CELL_SIZE - 1) / HASH_CELL_SIZE)
#define HASH_ROWS               ((LCD_HEIGHT + HASH_CELL_SIZE - 1) / HASH_CELL_SIZE)
#define HASH_CELLS              (HASH_COLUMNS * HASH_ROWS)
#define MATRIX_CELL_SIZE         16
#define MATRIX_COLUMNS           ((LCD_WIDTH + MATRIX_CELL_SIZE - 1) / MATRIX_CELL_SIZE)
#define MATRIX_ROWS              ((LCD_HEIGHT + MATRIX_CELL_SIZE - 1) / MATRIX_CELL_SIZE)
#define MATRIX_CELLS             (MATRIX_COLUMNS * MATRIX_ROWS)

#define Q8(value)               ((int32_t)(value) << 8)

typedef struct {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
} fluid_particle_t;

typedef struct {
    uint8_t deep[3];
    uint8_t body[3];
    uint8_t shine[3];
} liquid_palette_t;

typedef struct {
    int x1;
    int y1;
    int x2;
    int y2;
} dirty_rect_t;

/* Every user-supplied home asset is raw RGB565LE at the panel's native
 * resolution.  One frame is a still; additional complete frames form a
 * looping animation.  Keeping the source in this format makes it possible to
 * stream a 35 MB animation from NAND without a JPEG/GIF decoder or an
 * oversized RAM allocation. */
typedef struct {
    char name[HOME_MEDIA_NAME_MAX];
    char path[HOME_MEDIA_PATH_MAX];
    uint32_t frames;
    uint8_t fps;
} home_external_media_t;

/* NAND reads take about 200 ms for one native frame.  The reader task owns
 * those reads so the UI task can continue polling the touch controller. */
typedef struct {
    uint8_t media_index;
    uint32_t frame;
} home_media_request_t;

typedef enum {
    /* The default watchface is media; a top-down pull opens the app grid. */
    UI_SCREEN_HOME,
    UI_SCREEN_APPS,
    UI_SCREEN_FLUID,
    UI_SCREEN_FLUID_SETTINGS,
    UI_SCREEN_COLOUR_PICKER,
    UI_SCREEN_OUO,
    UI_SCREEN_OUO_MENU,
    UI_SCREEN_USB_DISK,
    UI_SCREEN_SKYORB,
    UI_SCREEN_WATCH,
    UI_SCREEN_CAMERA,
    UI_SCREEN_MIC_TEST,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_WIFI_SETTINGS,
} ui_screen_t;

typedef struct {
    float lat;
    float lon;
    float heading_deg;
    float track_deg;
    float speed_knots;
    char callsign[10];
    char altitude[12];
} skyorb_aircraft_t;

typedef struct {
    char ssid[33];
    char password[65];
} wifi_profile_t;

#define WIFI_PROFILE_MAX 5U

typedef struct {
    /* ssid/password/wifi_valid are the selected profile mirrored for the
     * existing radar/network code.  Profile management stays behind the
     * helpers below so callers do not need to understand the NVS layout. */
    char ssid[33];
    char password[65];
    wifi_profile_t wifi_profiles[WIFI_PROFILE_MAX];
    uint8_t wifi_count;
    uint8_t wifi_selected;
    float latitude;
    float longitude;
    uint8_t range_index;
    bool wifi_valid;
    bool location_valid;
    bool location_auto;
} skyorb_config_t;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    bool valid;
} clock_time_t;

typedef enum {
    CLOCK_SOURCE_RTC = 0,
    CLOCK_SOURCE_CUSTOM,
    CLOCK_SOURCE_BLE,
    CLOCK_SOURCE_WIFI,
} clock_source_t;

typedef enum {
    CLOCK_SYNC_IDLE = 0,
    CLOCK_SYNC_WAIT_WIFI,
    CLOCK_SYNC_RUNNING,
    CLOCK_SYNC_OK,
    CLOCK_SYNC_FAILED,
} clock_sync_state_t;

typedef enum {
    FLUID_SHAPE_SIMPLE,
    FLUID_SHAPE_BLOCKS,
    FLUID_SHAPE_MATRIX,
} fluid_shape_t;

typedef enum {
    OUO_EXPRESSION_IDLE,
    OUO_EXPRESSION_BLINK,
    OUO_EXPRESSION_WINK_LEFT,
    OUO_EXPRESSION_WINK_RIGHT,
    OUO_EXPRESSION_HAPPY,
    OUO_EXPRESSION_SQUISH,
    OUO_EXPRESSION_ANGRY,
    OUO_EXPRESSION_SURPRISED,
    OUO_EXPRESSION_SLEEPY,
    OUO_EXPRESSION_SAD,
    OUO_EXPRESSION_DIZZY,
    OUO_EXPRESSION_KISS,
    OUO_EXPRESSION_HEAD_PAT,
    OUO_EXPRESSION_DELIGHTED,
    OUO_EXPRESSION_CARET,
    OUO_EXPRESSION_MOUTH_RELEASE,
} ouo_expression_t;

/* A mouth drag is sampled once and then stays in its selected family for the
 * rest of that contact.  This is how the previewer avoids the jarring
 * surprise/side-pull reclassification seen when a finger bends mid-drag. */
typedef enum {
    OUO_MOUTH_VARIANT_ROUND = 0,
    OUO_MOUTH_VARIANT_STRETCH,
    OUO_MOUTH_VARIANT_SQUARE,
    OUO_MOUTH_VARIANT_TRIANGLE,
    OUO_MOUTH_VARIANT_SIDE,
} ouo_mouth_variant_t;

static esp_lcd_panel_handle_t s_lcd_panel;
static esp_lcd_panel_io_handle_t s_lcd_io;
static SemaphoreHandle_t s_lcd_done;
static uint16_t *s_lcd_stripe;
static uint16_t *s_lcd_stripe_secondary;
/* Native-endian RGB565 working surface.  It never goes directly to QSPI DMA:
 * each dirty section is byte-swapped into s_lcd_stripe immediately before
 * transfer, which preserves the stable internal-DMA behaviour of the factory
 * example. */
static uint16_t *s_lcd_canvas;
static size_t s_fluid_transfer_pixels;
static uint64_t s_home_perf_render_us;
static uint64_t s_home_perf_read_us;
static uint64_t s_home_perf_compose_us;
static uint64_t s_home_perf_dma_us;
static uint32_t s_home_perf_renders;
static uint32_t s_home_perf_reads;
static uint32_t s_home_perf_render_max_us;
static bool s_i2c_ready;
static bool s_imu_ready;
static bool s_touch_ready;
static bool s_axp2101_ready;
static bool s_sd_nand_mounted;
static sdmmc_card_t *s_sd_nand_card;
static esp_err_t s_sd_nand_mount_result = ESP_ERR_INVALID_STATE;
/* USB MSC owns this separate card descriptor after the firmware VFS has been
 * unmounted.  The host and the app must never access the NAND at the same
 * time, otherwise a copied picture could corrupt the FAT filesystem. */
static sdmmc_card_t *s_usb_msc_card;
static bool s_usb_msc_host_initialized;
static bool s_usb_msc_storage_initialized;
static bool s_usb_msc_started;
/* Visual intent is separate from driver readiness so the dedicated boot can
 * paint ON before TinyUSB owns the USB PHY. */
static bool s_usb_msc_ui_on;
static bool s_usb_msc_start_requested;
static bool s_usb_msc_reboot_requested;
static volatile bool s_usb_msc_exit_requested;
static bool s_usb_msc_exit_armed;
static TickType_t s_usb_msc_exit_arm_after;
static TickType_t s_usb_msc_touch_retry_after;
static bool s_usb_msc_exit_touch_down;
RTC_NOINIT_ATTR static uint32_t s_usb_msc_boot_magic;
static esp_err_t s_usb_msc_result = ESP_ERR_INVALID_STATE;
static ui_screen_t s_ui_screen = UI_SCREEN_HOME;
/* BLE submits requests; only the display task may change screens.  The
 * snapshot/result is protected separately from the queue for GET UI. */
static QueueHandle_t s_ble_ui_requests;
static portMUX_TYPE s_ble_ui_lock = portMUX_INITIALIZER_UNLOCKED;
static ui_screen_t s_ble_ui_snapshot = UI_SCREEN_HOME;
typedef enum { BLE_UI_IDLE, BLE_UI_PENDING, BLE_UI_DONE, BLE_UI_BUSY } ble_ui_state_t;
static ble_ui_state_t s_ble_ui_state = BLE_UI_IDLE;
static fluid_shape_t s_fluid_shape = FLUID_SHAPE_SIMPLE;
static bool s_home_dirty = true;
static bool s_apps_dirty;
static bool s_settings_dirty;
static bool s_colour_picker_dirty;
/* This tag describes the shared canvas, not the selected page. Other screens
 * invalidate it before using that canvas; wake also forces a full repaint. */
static ui_screen_t s_fluid_controls_canvas_screen = UI_SCREEN_HOME;
static int s_fluid_settings_drawn_shape = -1;
static int s_colour_marker_drawn_x;
static int s_colour_marker_drawn_y;
static bool s_colour_dragging;
static uint16_t *s_colour_wheel_cache;
static bool s_colour_wheel_cache_attempted;
static bool s_fluid_preferences_pending;
static int64_t s_fluid_preferences_retry_us;
static int64_t s_fluid_controls_input_us;
static struct {
    uint64_t draw_us, transfer_us, feedback_us, save_us, pixels;
    uint32_t frames, feedbacks, saves, max_feedback_us;
    int64_t report_us;
} s_fluid_ui_perf;
static bool s_ouo_dirty;
static bool s_ouo_menu_dirty;
static bool s_usb_disk_dirty;
static bool s_skyorb_dirty;
static bool s_watch_dirty;
static bool s_camera_dirty;
static bool s_mic_test_dirty;
static TickType_t s_mic_test_last_frame;
static esp_err_t s_mic_test_action_result = ESP_OK;
static uint8_t s_settings_debug_taps;
static TickType_t s_settings_debug_deadline;
static TickType_t s_camera_feedback_started;
static TickType_t s_camera_feedback_until;
static TickType_t s_camera_last_frame;
static esp_err_t s_camera_feedback_result = ESP_ERR_INVALID_STATE;
static uint8_t s_watch_style;
/* The BLE task may change the requested style while the UI task owns the
 * display.  Keeping the last style actually flushed lets the UI perform a
 * deterministic black-frame hand-off before drawing the other APK face. */
static uint8_t s_watch_rendered_style = UINT8_MAX;
static TickType_t s_watch_last_frame;
static int32_t s_watch_display_seconds = -1;
static clock_source_t s_clock_source = CLOCK_SOURCE_RTC;
static clock_sync_state_t s_clock_sync_state = CLOCK_SYNC_IDLE;
static clock_time_t s_clock_cached_time;
static bool s_clock_time_pending;
static clock_time_t s_clock_pending_time;
static clock_source_t s_clock_pending_source;
static bool s_clock_wifi_sync_requested;
static TickType_t s_clock_wifi_sync_started;
static bool s_alarm_enabled;
static uint8_t s_alarm_hour = 7;
static uint8_t s_alarm_minute;
static bool s_alarm_ringing;
static bool s_alarm_buzzer_ready;
static bool s_alarm_tone_on;
static TickType_t s_alarm_ring_deadline;
static TickType_t s_alarm_next_tone_toggle;
static int32_t s_alarm_last_date_key = -1;
static bool s_device_settings_dirty;
static bool s_wifi_settings_dirty;
static int s_settings_scroll_y;
static int s_settings_touch_origin_y;
static int s_settings_touch_last_y;
static bool s_settings_touch_dragging;
static uint8_t s_user_brightness = LCD_BRIGHTNESS_NORMAL;
static uint16_t s_display_sleep_timeout_seconds = DISPLAY_SLEEP_TIMEOUT_DEFAULT_SECONDS;
static volatile bool s_ble_brightness_pending;
static volatile uint8_t s_ble_pending_brightness;
static bool s_settings_full_refresh = true;
static bool s_settings_header_dirty;
static TickType_t s_settings_wifi_last_toggle;
static bool s_skyorb_network_started;
static bool s_skyorb_wifi_events_registered;
static esp_err_t s_skyorb_network_status = ESP_ERR_INVALID_STATE;
static bool s_skyorb_network_task_started;
/* Authoritative user-facing Wi-Fi switch state.  Connection/AP event flags
 * are transient and must not make the Settings switch appear to turn itself
 * off while the background radar task is still active. */
static volatile bool s_skyorb_wifi_enabled;
static bool s_skyorb_wifi_connected;
static uint8_t s_skyorb_disconnect_reason;
static bool s_wifi_scan_in_progress;
static esp_err_t s_wifi_scan_status = ESP_ERR_INVALID_STATE;
static bool s_wifi_profile_visible[WIFI_PROFILE_MAX];
static int8_t s_wifi_profile_rssi[WIFI_PROFILE_MAX];
/* Snapshot the exact 2.4 GHz BSS selected by the Wi-Fi page.  A router may
 * advertise one SSID from several radios/BSSIDs with different security
 * modes.  Reusing the scan result prevents esp_wifi_connect() from choosing
 * a different candidate than the row the user tapped. */
static wifi_ap_record_t s_wifi_profile_ap[WIFI_PROFILE_MAX];
static bool s_wifi_profile_ap_valid[WIFI_PROFILE_MAX];
static int s_wifi_delete_profile = -1;
static int s_wifi_touch_profile = -1;
static int64_t s_wifi_touch_started_us;
static bool s_wifi_touch_long_handled;
static volatile bool s_wifi_scan_requested;
static volatile bool s_wifi_should_connect;
static volatile bool s_wifi_connect_requested;
/* A profile switch is a two-phase operation.  The network task first leaves
 * the old AP and refreshes the selected BSS record, then starts association. */
static volatile bool s_wifi_connect_after_scan;
typedef enum {
    WIFI_LINK_IDLE = 0,
    WIFI_LINK_CONNECTING,
    WIFI_LINK_ASSOCIATED,
    WIFI_LINK_CONNECTED,
    WIFI_LINK_AUTH_FAILED,
    WIFI_LINK_NO_AP,
    WIFI_LINK_TIMEOUT,
    WIFI_LINK_FAILED,
} wifi_link_state_t;
typedef enum {
    SKYORB_LOCATION_UNSET = 0,
    SKYORB_LOCATION_REQUESTED,
    SKYORB_LOCATION_IN_PROGRESS,
    SKYORB_LOCATION_READY,
    SKYORB_LOCATION_ERR_CLIENT,
    SKYORB_LOCATION_ERR_OPEN,
    SKYORB_LOCATION_ERR_RESPONSE,
    SKYORB_LOCATION_ERR_HTTP,
    SKYORB_LOCATION_ERR_PARSE,
} skyorb_location_result_t;
static volatile wifi_link_state_t s_wifi_link_state;
static TickType_t s_wifi_connect_started;
static TickType_t s_wifi_settings_last_frame;
static bool s_skyorb_fetch_in_progress;
static bool s_skyorb_fetch_failed;
static const char *s_skyorb_fetch_failure_stage;
static volatile bool s_skyorb_location_in_progress;
static volatile bool s_skyorb_auto_location_attempted;
static volatile skyorb_location_result_t s_skyorb_location_result = SKYORB_LOCATION_UNSET;
static bool s_skyorb_config_loaded;
static bool s_skyorb_demo_mode = true;
static uint8_t s_skyorb_range_index = 1;
static uint16_t s_skyorb_sweep_angle;
static TickType_t s_skyorb_last_frame;
static bool s_skyorb_rotate_active;
static float s_skyorb_rotate_last_angle;
static float s_skyorb_rotate_accumulated;
static TickType_t s_skyorb_last_fetch;
static TickType_t s_skyorb_last_success;
static skyorb_config_t s_skyorb_config;
static skyorb_aircraft_t s_skyorb_aircraft[SKYORB_MAX_AIRCRAFT];
static size_t s_skyorb_aircraft_count;
static SemaphoreHandle_t s_skyorb_mutex;
static esp_netif_t *s_skyorb_sta_netif;
/* OLED protection state.  Touch remains active while the panel is blanked,
 * and the first touch only wakes the display rather than activating a UI
 * control underneath it. */
static TickType_t s_last_user_activity;
static TickType_t s_next_home_pixel_shift;
static TickType_t s_next_home_status_shift;
static bool s_display_dimmed;
static bool s_display_sleeping;
static bool s_display_wake_touch_suppressed;
static bool s_ouo_canvas_valid;
static ouo_expression_t s_ouo_expression = OUO_EXPRESSION_IDLE;
static int s_ouo_mood = 68;
static uint8_t s_ouo_preferred_mood = 68;
static int s_ouo_eye_pokes;
static int64_t s_ouo_expression_until_us;
static int64_t s_ouo_idle_epoch_us;
static uint8_t *s_ouo_idle_tiles;
static uint32_t s_ouo_idle_frame = UINT32_MAX;
static bool s_ouo_idle_visible;
static int64_t s_ouo_last_gaze_update_us;
static int64_t s_ouo_touch_started_us;
static int64_t s_ouo_render_total_us;
static uint32_t s_ouo_render_frames;
static uint16_t s_ouo_idle_palette[256];
static bool s_ouo_idle_palette_ready;
static int64_t s_ouo_paint_window_us;
static int64_t s_ouo_flush_window_us;
static int64_t s_ouo_work_window_us;
static int64_t s_ouo_update_last_us;
static int64_t s_ouo_decode_last_us;
static int64_t s_ouo_decode_window_us;
static int s_ouo_gaze_x;
static int s_ouo_gaze_y;
static int s_ouo_squish_pixels;
static int s_ouo_mouth_stretch_pixels;
static int s_ouo_mouth_x_offset;
static int s_ouo_mouth_y_offset;
static ouo_mouth_variant_t s_ouo_mouth_variant = OUO_MOUTH_VARIANT_STRETCH;
static bool s_ouo_mouth_variant_selected;
static bool s_ouo_mouth_side;
static int s_ouo_shake_x;
static int s_ouo_shake_y;
static int64_t s_ouo_shake_until_us;
static int64_t s_ouo_last_shake_us;
/* DIZZY is a live animation, not a static eye glyph.  Keep its phase in the
 * renderer state so both eyes rotate together and the dirty flag can request
 * a fresh frame while the expression is active. */
static float s_ouo_dizzy_phase;
static int64_t s_ouo_last_dizzy_phase_us;
/* A normal tilt is one smooth acceleration.  A deliberate shake alternates
 * direction several times in a short interval, so retain just enough motion
 * history to distinguish the two. */
static int s_ouo_last_motion_dx;
static int s_ouo_last_motion_dy;
static uint8_t s_ouo_shake_reversals;
static int64_t s_ouo_shake_window_started_us;
static bool s_ouo_mouth_touch_active;
/* -1/1 remembers which eye was touched so a short tap can wink while a
 * longer hold can still promote the same contact to surprised. */
static int8_t s_ouo_eye_touch_side;
static int s_ouo_touch_origin_x;
static int s_ouo_touch_origin_y;
static int s_ouo_last_touch_x;
static int s_ouo_last_touch_y;
static int s_ouo_last_gravity_x;
static int s_ouo_last_gravity_y;
static bool s_ouo_touch_active;
static bool s_ouo_menu_hold_armed;
/* These mirror the useful, unobtrusive controls from OuO's hidden options:
 * auto keeps the idle face alive, while tilt controls both gaze and the
 * stronger motion reaction.  They deliberately default on. */
static bool s_ouo_auto_expressions = true;
static bool s_ouo_tilt_reactions = true;
static uint16_t s_battery_mv;
static uint8_t s_battery_percent;
static bool s_battery_percent_valid;
static bool s_vbus_present;
static bool s_battery_charging;
static uint8_t s_palette_rotation;
static bool s_custom_palette_active;
static float s_colour_hue = 0.54f;
static float s_colour_saturation = 0.86f;
static int s_touch_x = LCD_WIDTH / 2;
static int s_touch_y = LCD_HEIGHT / 2;
static uint8_t s_touch_energy;
static bool s_touch_down;
/* A gesture that changes pages owns the contact until the controller reports
 * a real release.  Without this guard, a release sampled during a long DMA
 * flush can leave s_touch_down set and the next finger-down is interpreted as
 * the continuation of the previous swipe. */
static bool s_touch_blocked_until_release;
static bool s_home_pull_armed;
static bool s_home_swipe_handled;
static bool s_apps_swipe_handled;
static bool s_apps_dismiss_active;
static uint8_t s_apps_dismiss_step;
static bool s_apps_entrance_active;
static uint8_t s_apps_entrance_step;
static int s_apps_swipe_origin_x;
static int s_apps_swipe_origin_y;
/* The launcher is static while it is visible.  Keep one native-endian frame
 * in PSRAM so the upward return animation does not re-run the complete icon
 * geometry for every pixel at every step. */
static bool s_apps_canvas_valid;
static const uint16_t *s_apps_home_transition_frame;
static uint8_t s_home_media_index;
static int8_t s_home_media_shift_x;
static int8_t s_home_media_shift_y;
static uint8_t s_home_media_shift_phase;
static int8_t s_home_status_shift_x;
static int8_t s_home_status_shift_y;
static uint8_t s_home_status_shift_phase;
static uint8_t s_home_slide_from_index;
static uint32_t s_home_slide_from_frame;
static uint8_t s_home_slide_step;
static int8_t s_home_slide_direction;
static bool s_home_slide_active;
static bool s_home_slide_waiting;
static uint8_t s_home_pending_slide_from;
static uint8_t s_home_pending_slide_to;
static uint32_t s_home_pending_slide_from_frame;
static int8_t s_home_pending_slide_direction;
static int s_home_pull_origin_x;
static int s_home_pull_origin_y;
static home_external_media_t s_home_external_media[HOME_EXTERNAL_MEDIA_MAX];
static uint8_t s_home_external_media_count;
/* Four PSRAM frames preserve the displayed page and the swipe source while a
 * lower-priority task streams a replacement from U2. */
static uint16_t *s_home_media_cache[HOME_MEDIA_CACHE_SLOTS];
static uint8_t s_home_media_cache_index[HOME_MEDIA_CACHE_SLOTS] = {
    [0 ... HOME_MEDIA_CACHE_SLOTS - 1] = HOME_MEDIA_INVALID_INDEX,
};
static uint32_t s_home_media_cache_frame[HOME_MEDIA_CACHE_SLOTS] = {
    [0 ... HOME_MEDIA_CACHE_SLOTS - 1] = UINT32_MAX,
};
static QueueHandle_t s_home_media_request_queue;
static bool s_home_media_reader_started;
static volatile bool s_home_media_reader_busy;
static volatile bool s_home_media_io_enabled;
static volatile uint8_t s_home_media_loading_index = HOME_MEDIA_INVALID_INDEX;
static volatile uint32_t s_home_media_loading_frame = UINT32_MAX;
static uint8_t s_home_media_queued_index = HOME_MEDIA_INVALID_INDEX;
static uint32_t s_home_media_queued_frame = UINT32_MAX;
static volatile bool s_home_media_rescan_pending;
static FILE *s_ble_media_file;
static char s_ble_media_temp_path[HOME_MEDIA_PATH_MAX];
static char s_ble_media_final_path[HOME_MEDIA_PATH_MAX];
static char s_ble_media_name[HOME_MEDIA_NAME_MAX];
static uint32_t s_ble_media_expected_size;
static uint32_t s_ble_media_expected_frames;
static uint8_t s_ble_media_expected_fps;
static uint32_t s_ble_media_received_size;
static uint32_t s_ble_media_binary_packets;
static bool s_ble_media_transfer_active;
static uint32_t s_home_animation_frame;
static TickType_t s_home_next_animation_tick;

static fluid_particle_t s_particles[PARTICLE_COUNT];
static int16_t s_drawn_x[PARTICLE_COUNT];
static int16_t s_drawn_y[PARTICLE_COUNT];
static int16_t s_matrix_x[PARTICLE_COUNT];
static int16_t s_matrix_y[PARTICLE_COUNT];
static bool s_matrix_layout_ready;
static bool s_fluid_canvas_valid;
static uint16_t s_hash_count[HASH_CELLS];
static uint16_t s_hash_start[HASH_CELLS + 1];
static uint16_t s_hash_cursor[HASH_CELLS];
static uint16_t s_hash_particle[PARTICLE_COUNT];
static bool s_matrix_cell_used[MATRIX_CELLS];

static esp_err_t i2c_read(uint8_t address, uint8_t reg, uint8_t *data, size_t length);
static int clamp_int(int value, int low, int high);
static void read_tilt(int *gravity_x, int *gravity_y);
static void skyorb_enter(void);
static void skyorb_handle_touch(int x, int y);
static void skyorb_handle_touch_move(int x, int y);
static void skyorb_handle_touch_release(void);
static void skyorb_render_frame(void);
static void watch_enter(void);
static void watch_handle_touch(int x, int y);
static void watch_render_frame(void);
static void camera_enter(void);
static void camera_handle_touch(int x, int y);
static void camera_render_frame(void);
static void mic_test_enter(void);
static void mic_test_handle_touch(int x, int y);
static void mic_test_render_frame(void);
static int32_t watch_advance_display_time(int32_t displayed, int32_t target,
                                          bool *catch_up_pending);
static clock_time_t clock_read_rtc(void);
static void clock_load_preferences(void);
static void clock_service(TickType_t now);
static void clock_save_preferences(void);
static void alarm_stop(void);
static void settings_enter(void);
static void settings_handle_touch(int x, int y);
static void render_device_settings_frame(void);
static void wifi_settings_enter(void);
static void wifi_settings_handle_touch(int x, int y);
static void wifi_settings_update_touch(int x, int y);
static void wifi_settings_handle_release(void);
static bool wifi_request_profile_connection(uint8_t index);
static void render_wifi_settings_frame(void);
static void settings_load_preferences(void);
static void block_touch_until_release(void);
static esp_err_t fluid_ble_command(const char *command, char *response,
                                   size_t response_size);

/* The palettes deliberately stay within one hue family each. */
static const liquid_palette_t s_liquid_palettes[] = {
    {{1, 11, 39}, {31, 169, 237}, {196, 246, 255}},
    {{10, 5, 38}, {116, 82, 236}, {224, 210, 255}},
    {{0, 17, 25}, {10, 172, 180}, {172, 255, 232}},
};

static liquid_palette_t s_custom_palette;

static void hsv_to_rgb(float hue, float saturation, float value,
                       uint8_t *red, uint8_t *green, uint8_t *blue)
{
    hue -= floorf(hue);
    saturation = saturation < 0.0f ? 0.0f : (saturation > 1.0f ? 1.0f : saturation);
    value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    float chroma = value * saturation;
    float sector = hue * 6.0f;
    float intermediate = chroma * (1.0f - fabsf(fmodf(sector, 2.0f) - 1.0f));
    float match = value - chroma;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (sector < 1.0f) {
        r = chroma; g = intermediate;
    } else if (sector < 2.0f) {
        r = intermediate; g = chroma;
    } else if (sector < 3.0f) {
        g = chroma; b = intermediate;
    } else if (sector < 4.0f) {
        g = intermediate; b = chroma;
    } else if (sector < 5.0f) {
        r = intermediate; b = chroma;
    } else {
        r = chroma; b = intermediate;
    }
    *red = (uint8_t)clamp_int((int)((r + match) * 255.0f + 0.5f), 0, 255);
    *green = (uint8_t)clamp_int((int)((g + match) * 255.0f + 0.5f), 0, 255);
    *blue = (uint8_t)clamp_int((int)((b + match) * 255.0f + 0.5f), 0, 255);
}

static void set_custom_palette(float hue, float saturation)
{
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    s_colour_hue = hue - floorf(hue);
    s_colour_saturation = saturation < 0.15f ? 0.15f :
                          (saturation > 1.0f ? 1.0f : saturation);
    hsv_to_rgb(s_colour_hue, s_colour_saturation, 0.20f,
               &s_custom_palette.deep[0], &s_custom_palette.deep[1],
               &s_custom_palette.deep[2]);
    hsv_to_rgb(s_colour_hue, s_colour_saturation, 0.92f,
               &s_custom_palette.body[0], &s_custom_palette.body[1],
               &s_custom_palette.body[2]);
    hsv_to_rgb(s_colour_hue, s_colour_saturation * 0.18f, 1.0f,
               &red, &green, &blue);
    s_custom_palette.shine[0] = red;
    s_custom_palette.shine[1] = green;
    s_custom_palette.shine[2] = blue;
    s_custom_palette_active = true;
}

static const liquid_palette_t *active_palette(void)
{
    return s_custom_palette_active ? &s_custom_palette :
           &s_liquid_palettes[s_palette_rotation];
}

static bool fluid_save_preferences(void)
{
    const pacon_fluid_preferences_t value = {
        .shape = (uint8_t)s_fluid_shape,
        .custom = s_custom_palette_active,
        .hue = (uint16_t)clamp_int((int)lroundf(s_colour_hue * 10000.0f), 0, 9999),
        .saturation = (uint16_t)clamp_int((int)lroundf(s_colour_saturation * 10000.0f), 1500, 10000),
    };
    return pacon_save_fluid_preferences(&value) == ESP_OK;
}

static void fluid_queue_preferences(void)
{
    s_fluid_preferences_pending = true;
    if (s_fluid_controls_input_us == 0)
        s_fluid_controls_input_us = esp_timer_get_time();
}

/* Called AFTER rendering. Keep the last dragged colour in RAM until release,
 * then commit once. A failed commit stays pending and retries without a loop
 * of flash writes. Works even if BLE switches away from the controls page. */
static void fluid_service_preferences(void)
{
    const int64_t now = esp_timer_get_time();
    if (!s_fluid_preferences_pending || s_touch_down ||
        now < s_fluid_preferences_retry_us) return;
    if (fluid_save_preferences()) {
        s_fluid_preferences_pending = false;
        s_fluid_preferences_retry_us = 0;
    } else {
        s_fluid_preferences_retry_us = now + 1000000;
    }
    s_fluid_ui_perf.save_us += esp_timer_get_time() - now;
    ++s_fluid_ui_perf.saves;
}

static void ouo_save_preferences(void)
{
    const pacon_ouo_preferences_t value = {
        .automatic = s_ouo_auto_expressions, .tilt = s_ouo_tilt_reactions,
        .mood = s_ouo_preferred_mood,
    };
    (void)pacon_save_ouo_preferences(&value);
}

static void feature_load_preferences(void)
{
    pacon_fluid_preferences_t fluid = {
        .shape = FLUID_SHAPE_SIMPLE, .custom = false, .hue = 5400, .saturation = 8600,
    };
    pacon_load_fluid_preferences(&fluid);
    s_fluid_shape = (fluid_shape_t)fluid.shape;
    s_colour_hue = (float)fluid.hue / 10000.0f;
    s_colour_saturation = (float)fluid.saturation / 10000.0f;
    if (fluid.custom) set_custom_palette(s_colour_hue, s_colour_saturation);
    else s_custom_palette_active = false;
    pacon_ouo_preferences_t ouo = {.automatic = true, .tilt = true, .mood = 68};
    pacon_load_ouo_preferences(&ouo);
    s_ouo_auto_expressions = ouo.automatic;
    s_ouo_tilt_reactions = ouo.tilt;
    s_ouo_preferred_mood = ouo.mood;
    s_ouo_mood = ouo.mood;
    ESP_LOGI(TAG, "Preferences restored: fluid=%u custom=%d ouo_auto=%d tilt=%d mood=%u",
             fluid.shape, fluid.custom, ouo.automatic, ouo.tilt, ouo.mood);
}

static const sh8601_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x63, (uint8_t[]){0xFF}, 1, 10},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x08, 0x01, 0xD9}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x29, (uint8_t[]){0x00}, 0, 10},
};

static esp_err_t lcd_set_brightness(uint8_t brightness)
{
    if (s_lcd_io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* SH8601 QSPI does not accept a bare DCS command.  Match the bundled
     * driver's tx_param() encoding: opcode 0x02 + command byte on D[15:8]. */
    const uint32_t command = (0x02UL << 24) | (0x51UL << 8);
    return esp_lcd_panel_io_tx_param(s_lcd_io, command, &brightness, 1);
}

static esp_err_t lcd_hold_power_off(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << PIN_LCD_POWER,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err == ESP_OK) err = gpio_set_level(PIN_LCD_POWER, 0);
    return err;
}

static void settings_load_preferences(void)
{
    nvs_handle_t nvs;
    if (nvs_open("pacon_ui", NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    uint8_t brightness = LCD_BRIGHTNESS_NORMAL;
    if (nvs_get_u8(nvs, "brightness", &brightness) == ESP_OK) {
        s_user_brightness = (uint8_t)clamp_int(brightness,
                                                SETTINGS_BRIGHTNESS_MIN,
                                                SETTINGS_BRIGHTNESS_MAX);
    }
    uint16_t timeout_seconds = DISPLAY_SLEEP_TIMEOUT_DEFAULT_SECONDS;
    if (nvs_get_u16(nvs, "sleep_s", &timeout_seconds) == ESP_OK &&
        (timeout_seconds == 0U || timeout_seconds == 15U ||
         timeout_seconds == 30U || timeout_seconds == 60U ||
         timeout_seconds == 120U || timeout_seconds == 300U)) {
        s_display_sleep_timeout_seconds = timeout_seconds;
    }
    nvs_close(nvs);
}

static void settings_save_brightness(void)
{
    nvs_handle_t nvs;
    if (nvs_open("pacon_ui", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    (void)nvs_set_u8(nvs, "brightness", s_user_brightness);
    (void)nvs_commit(nvs);
    nvs_close(nvs);
}

static void settings_save_display_timeout(void)
{
    nvs_handle_t nvs;
    if (nvs_open("pacon_ui", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    (void)nvs_set_u16(nvs, "sleep_s", s_display_sleep_timeout_seconds);
    (void)nvs_commit(nvs);
    nvs_close(nvs);
}

static void apply_ble_brightness_if_pending(void)
{
    if (!s_ble_brightness_pending) return;
    const uint8_t brightness = s_ble_pending_brightness;
    s_ble_brightness_pending = false;
    s_user_brightness = (uint8_t)clamp_int(brightness,
                                           SETTINGS_BRIGHTNESS_MIN,
                                           SETTINGS_BRIGHTNESS_MAX);
    if (lcd_set_brightness(s_user_brightness) == ESP_OK) {
        s_display_dimmed = false;
        s_last_user_activity = xTaskGetTickCount();
        settings_save_brightness();
        s_home_dirty = true;
        s_device_settings_dirty = true;
        ESP_LOGI(TAG, "BLE: applied brightness register=0x%02X", s_user_brightness);
    } else {
        ESP_LOGW(TAG, "BLE: brightness update failed");
    }
}

static void display_note_activity(void)
{
    s_last_user_activity = xTaskGetTickCount();
    bool woke_panel = false;
    if (s_display_sleeping && s_lcd_panel != NULL) {
        if (esp_lcd_panel_disp_on_off(s_lcd_panel, true) == ESP_OK) {
            s_display_sleeping = false;
            woke_panel = true;
            ESP_LOGI(TAG, "OLED: panel woke on touch");

            /* SH8601 display-off/on is not guaranteed to preserve the visible
             * frame on every panel revision.  Force the active screen through
             * its normal renderer after wake instead of assuming GRAM is still
             * visible. */
            switch (s_ui_screen) {
            case UI_SCREEN_HOME: s_home_dirty = true; break;
            case UI_SCREEN_APPS: s_apps_dirty = true; break;
            case UI_SCREEN_FLUID_SETTINGS:
                s_fluid_controls_canvas_screen = UI_SCREEN_HOME;
                s_settings_dirty = true; break;
            case UI_SCREEN_COLOUR_PICKER:
                s_fluid_controls_canvas_screen = UI_SCREEN_HOME;
                s_colour_picker_dirty = true; break;
            case UI_SCREEN_OUO: s_ouo_dirty = true; break;
            case UI_SCREEN_OUO_MENU: s_ouo_menu_dirty = true; break;
            case UI_SCREEN_USB_DISK: s_usb_disk_dirty = true; break;
            case UI_SCREEN_SKYORB: s_skyorb_dirty = true; break;
            case UI_SCREEN_WATCH: s_watch_dirty = true; break;
            case UI_SCREEN_SETTINGS: s_device_settings_dirty = true; break;
            case UI_SCREEN_WIFI_SETTINGS: s_wifi_settings_dirty = true; break;
            case UI_SCREEN_FLUID:
            default:
                break;
            }
        }
    }
    if (!s_display_sleeping && (s_display_dimmed || woke_panel)) {
        if (lcd_set_brightness(s_user_brightness) == ESP_OK) {
            s_display_dimmed = false;
            ESP_LOGI(TAG, "OLED: restored normal brightness");
        }
    }
}

static void display_update_idle(TickType_t now)
{
    if (s_lcd_panel == NULL || s_ui_screen == UI_SCREEN_USB_DISK ||
        s_last_user_activity == 0 || s_display_sleeping) {
        return;
    }

    const TickType_t idle = now - s_last_user_activity;
    const uint32_t sleep_timeout_ms =
        (uint32_t)s_display_sleep_timeout_seconds * 1000U;
    if (sleep_timeout_ms != 0U && idle >= pdMS_TO_TICKS(sleep_timeout_ms)) {
        if (esp_lcd_panel_disp_on_off(s_lcd_panel, false) == ESP_OK) {
            s_display_sleeping = true;
            ESP_LOGI(TAG, "OLED: panel blanked after %lu ms idle",
                     (unsigned long)sleep_timeout_ms);
        }
        return;
    }
    if (!s_display_dimmed && idle >= pdMS_TO_TICKS(DISPLAY_DIM_TIMEOUT_MS) &&
        lcd_set_brightness(LCD_BRIGHTNESS_DIM) == ESP_OK) {
        s_display_dimmed = true;
        ESP_LOGI(TAG, "OLED: dimmed after %d ms idle", DISPLAY_DIM_TIMEOUT_MS);
    }
}

static int clamp_int(int value, int low, int high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static uint16_t rgb565(int red, int green, int blue)
{
    red = clamp_int(red, 0, 255);
    green = clamp_int(green, 0, 255);
    blue = clamp_int(blue, 0, 255);
    return (uint16_t)(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
}

/* ESP32-S3 stores a uint16_t little-endian, whereas this SH8601 QSPI path
 * consumes RGB565 pixels most-significant byte first. */
static uint16_t rgb565_for_sh8601(uint16_t colour)
{
    return (uint16_t)((colour << 8) | (colour >> 8));
}

/* The home screen shares its direct-QSPI stripe renderer with the fluid.
 * These helpers read LVGL's 4 bpp Montserrat glyph data directly, so text is
 * anti-aliased without creating an LVGL display or a second frame buffer. */
static uint16_t rgb565_blend(uint16_t background, uint16_t foreground, uint8_t alpha)
{
    uint32_t inverse = 255U - alpha;
    uint32_t red = (((background >> 11) & 0x1FU) * inverse +
                    ((foreground >> 11) & 0x1FU) * alpha + 127U) / 255U;
    uint32_t green = (((background >> 5) & 0x3FU) * inverse +
                      ((foreground >> 5) & 0x3FU) * alpha + 127U) / 255U;
    uint32_t blue = ((background & 0x1FU) * inverse +
                     (foreground & 0x1FU) * alpha + 127U) / 255U;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static uint8_t home_font_alpha(int x, int y, int origin_x, int line_top,
                               const lv_font_t *font, const char *text)
{
    if (x < origin_x || y < line_top || y >= line_top + font->line_height) {
        return 0;
    }

    int pen_x = origin_x;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        lv_font_glyph_dsc_t glyph;
        uint32_t next = (uint8_t)text[index + 1];
        if (!lv_font_get_glyph_dsc(font, &glyph, (uint8_t)text[index], next)) {
            continue;
        }

        int glyph_left = pen_x + glyph.ofs_x;
        int glyph_top = line_top + (font->line_height - font->base_line) -
                        glyph.box_h - glyph.ofs_y;
        if (x >= glyph_left && x < glyph_left + glyph.box_w &&
            y >= glyph_top && y < glyph_top + glyph.box_h) {
            const uint8_t *bitmap = lv_font_get_glyph_bitmap(glyph.resolved_font,
                                                               (uint8_t)text[index]);
            if (bitmap == NULL) {
                return 0;
            }
            size_t pixel_index = (size_t)(y - glyph_top) * glyph.box_w +
                                 (size_t)(x - glyph_left);
            if (glyph.bpp == 4) {
                uint8_t sample = bitmap[pixel_index >> 1];
                sample = (pixel_index & 1U) ? (sample & 0x0FU) : (sample >> 4);
                return (uint8_t)(sample * 17U);
            }
            if (glyph.bpp == 1) {
                return (bitmap[pixel_index >> 3] & (0x80U >> (pixel_index & 7U))) ? 255 : 0;
            }
        }
        /* lv_font_get_glyph_dsc() already converts advance width from the
         * font's 1/16-pixel storage unit to whole display pixels.  Dividing
         * it again makes successive letters overlap at roughly one pixel. */
        pen_x += glyph.adv_w;
    }
    return 0;
}

static bool home_in_circle(int x, int y, int center_x, int center_y, int radius)
{
    int dx = x - center_x;
    int dy = y - center_y;
    return dx * dx + dy * dy <= radius * radius;
}

static bool home_in_round_rect(int x, int y, int left, int top,
                               int right, int bottom, int radius)
{
    if (x < left || x > right || y < top || y > bottom) {
        return false;
    }
    int near_x = x < left + radius ? left + radius : (x > right - radius ? right - radius : x);
    int near_y = y < top + radius ? top + radius : (y > bottom - radius ? bottom - radius : y);
    return home_in_circle(x, y, near_x, near_y, radius);
}

static char s_home_battery[5] = "--";

static void prepare_home_labels(void)
{
    if (s_battery_percent_valid) {
        snprintf(s_home_battery, sizeof(s_home_battery), "%u%%", s_battery_percent);
    } else {
        strcpy(s_home_battery, "--");
    }
}

static uint8_t home_media_count(void)
{
    return s_home_external_media_count > 0 ? s_home_external_media_count :
                                             HOME_FALLBACK_MEDIA_COUNT;
}

static bool home_media_uses_nand(void)
{
    return s_home_external_media_count > 0;
}

static bool home_is_rgb565_name(const char *name)
{
    const size_t length = strlen(name);
    /* Accept both the canonical extension and the 8.3 alias extension.  The
     * latter keeps media copied while an older no-LFN firmware was installed
     * usable after upgrading.  File size validation remains mandatory. */
    static const char long_extension[] = ".rgb565";
    static const char short_extension[] = ".rgb";
    const char *extension = NULL;
    if (length >= sizeof(long_extension) - 1) {
        extension = name + length - (sizeof(long_extension) - 1);
        if (tolower((unsigned char)extension[0]) == '.' &&
            tolower((unsigned char)extension[1]) == 'r' &&
            tolower((unsigned char)extension[2]) == 'g' &&
            tolower((unsigned char)extension[3]) == 'b' &&
            tolower((unsigned char)extension[4]) == '5' &&
            tolower((unsigned char)extension[5]) == '6' &&
            tolower((unsigned char)extension[6]) == '5') {
            return true;
        }
    }
    if (length >= sizeof(short_extension) - 1) {
        extension = name + length - (sizeof(short_extension) - 1);
        return tolower((unsigned char)extension[0]) == '.' &&
               tolower((unsigned char)extension[1]) == 'r' &&
               tolower((unsigned char)extension[2]) == 'g' &&
               tolower((unsigned char)extension[3]) == 'b';
    }
    return false;
}

static uint8_t home_fps_from_name(const char *name)
{
    const char *fps_mark = strstr(name, "fps");
    if (fps_mark == NULL) {
        return 8;
    }
    const char *number = fps_mark;
    while (number > name && isdigit((unsigned char)number[-1])) {
        --number;
    }
    const int fps = atoi(number);
    return fps >= 1 && fps <= 12 ? (uint8_t)fps : 8;
}

static void scan_home_external_media(void)
{
    s_home_external_media_count = 0;
    if (!s_sd_nand_mounted) {
        return;
    }

    char directory_path[HOME_MEDIA_PATH_MAX];
    snprintf(directory_path, sizeof(directory_path), "%s/media", SD_NAND_MOUNT_POINT);
    DIR *directory = opendir(directory_path);
    if (directory == NULL) {
        ESP_LOGI(TAG, "Media: no %s directory; using Miku-teal fallback", directory_path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL &&
           s_home_external_media_count < HOME_EXTERNAL_MEDIA_MAX) {
        if (entry->d_name[0] == '.' || !home_is_rgb565_name(entry->d_name)) {
            continue;
        }

        home_external_media_t *media =
            &s_home_external_media[s_home_external_media_count];
        const int written = snprintf(media->path, sizeof(media->path), "%s/%s",
                                     directory_path, entry->d_name);
        if (written < 0 || written >= (int)sizeof(media->path)) {
            ESP_LOGW(TAG, "Media: path too long, skipped: %s", entry->d_name);
            continue;
        }

        struct stat info;
        if (stat(media->path, &info) != 0 || !S_ISREG(info.st_mode) ||
            info.st_size <= 0 || ((uint64_t)info.st_size % LCD_FRAME_BYTES) != 0) {
            ESP_LOGW(TAG, "Media: skipped %s (expected N x %u B RGB565 frames)",
                     entry->d_name, (unsigned)LCD_FRAME_BYTES);
            continue;
        }

        const uint64_t frames = (uint64_t)info.st_size / LCD_FRAME_BYTES;
        if (frames > UINT32_MAX) {
            ESP_LOGW(TAG, "Media: too many frames, skipped: %s", entry->d_name);
            continue;
        }

        strncpy(media->name, entry->d_name, sizeof(media->name) - 1);
        media->name[sizeof(media->name) - 1] = '\0';
        media->frames = (uint32_t)frames;
        media->fps = frames > 1 ? home_fps_from_name(media->name) : 0;
        ESP_LOGI(TAG, "Media: %s %s, %u frame(s)%s%u fps",
                 frames > 1 ? "animation" : "image", media->name,
                 (unsigned)media->frames, frames > 1 ? " at " : "",
                 (unsigned)media->fps);
        ++s_home_external_media_count;
    }
    closedir(directory);

    ESP_LOGI(TAG, "Media: %u valid NAND item(s) in %s; %s",
             (unsigned)s_home_external_media_count, directory_path,
             s_home_external_media_count > 0 ? "using NAND carousel" :
                                               "using Miku-teal fallback");
}

static bool home_media_pause_reader(void)
{
    if (s_home_media_request_queue == NULL) return true;
    s_home_media_io_enabled = false;
    for (int retry = 0; s_home_media_reader_busy && retry < 40; ++retry) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (s_home_media_reader_busy) {
        s_home_media_io_enabled = true;
        return false;
    }
    return true;
}

static void home_media_resume_reader(void)
{
    if (s_home_media_request_queue != NULL) {
        s_home_media_io_enabled = true;
    }
}

static void home_media_abort_ble_transfer(bool remove_temp)
{
    if (s_ble_media_file != NULL) {
        fclose(s_ble_media_file);
        s_ble_media_file = NULL;
    }
    if (remove_temp && s_ble_media_temp_path[0] != '\0') {
        (void)remove(s_ble_media_temp_path);
    }
    s_ble_media_transfer_active = false;
    s_ble_media_temp_path[0] = '\0';
    s_ble_media_final_path[0] = '\0';
    s_ble_media_name[0] = '\0';
    s_ble_media_expected_size = 0;
    s_ble_media_expected_frames = 0;
    s_ble_media_expected_fps = 0;
    s_ble_media_received_size = 0;
    s_ble_media_binary_packets = 0;
    home_media_resume_reader();
}

static bool ble_media_name_valid(const char *name)
{
    if (name == NULL || name[0] == '\0' || name[0] == '.') return false;
    const size_t length = strlen(name);
    if (length >= HOME_MEDIA_NAME_MAX || !home_is_rgb565_name(name)) return false;
    for (size_t index = 0; index < length; ++index) {
        const unsigned char value = (unsigned char)name[index];
        if (!(isalnum(value) || value == '_' || value == '-' || value == '.')) {
            return false;
        }
    }
    return true;
}

typedef struct {
    char name[HOME_MEDIA_NAME_MAX];
    uint32_t size;
    uint32_t frames;
    uint8_t fps;
} ble_media_entry_t;

/* Kept out of the NimBLE access callback stack.  The callback is serialized
 * by the NimBLE host, so a single command-side snapshot is sufficient. */
static ble_media_entry_t s_ble_media_command_entries[HOME_EXTERNAL_MEDIA_MAX];
static char s_ble_media_command_name[HOME_MEDIA_NAME_MAX];
static char s_ble_media_command_fields[244];
static uint8_t s_ble_media_command_bytes[108];
static char s_ble_media_scan_path[HOME_MEDIA_PATH_MAX];
static char s_ble_command_text[244];
static char s_ble_wifi_credentials[224];

/* Enumerate the on-NAND media directory without touching the carousel array.
 * BLE commands can arrive while the UI is rendering; keeping this snapshot
 * local avoids racing the renderer's cached entry list. */
static size_t ble_collect_media_entries(ble_media_entry_t *entries, size_t capacity)
{
    if (entries == NULL || capacity == 0U || !s_sd_nand_mounted) return 0U;
    DIR *directory = opendir(SD_NAND_MOUNT_POINT "/media");
    if (directory == NULL) return 0U;

    size_t count = 0U;
    struct dirent *entry;
    while (count < capacity && (entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.' || !home_is_rgb565_name(entry->d_name)) {
            continue;
        }
        const int written = snprintf(s_ble_media_scan_path,
                                     sizeof(s_ble_media_scan_path), "%s/media/%s",
                                     SD_NAND_MOUNT_POINT, entry->d_name);
        if (written < 0 || written >= (int)sizeof(s_ble_media_scan_path)) continue;
        struct stat info;
        if (stat(s_ble_media_scan_path, &info) != 0 || !S_ISREG(info.st_mode) ||
            info.st_size <= 0 || ((uint64_t)info.st_size % LCD_FRAME_BYTES) != 0) {
            continue;
        }
        const uint64_t frames = (uint64_t)info.st_size / LCD_FRAME_BYTES;
        if (frames > UINT32_MAX) continue;
        ble_media_entry_t *media = &entries[count++];
        const size_t name_length = strnlen(entry->d_name, sizeof(media->name) - 1U);
        memcpy(media->name, entry->d_name, name_length);
        media->name[name_length] = '\0';
        media->size = (uint32_t)info.st_size;
        media->frames = (uint32_t)frames;
        media->fps = frames > 1U ? home_fps_from_name(media->name) : 0U;
    }
    closedir(directory);
    return count;
}

static int ble_media_hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

/* High-throughput companion to MEDIA_DATA.  The first four bytes are the
 * little-endian file offset; the remaining bytes are raw RGB565LE.  The
 * Android client sends eight packets per window and waits for the cumulative
 * acknowledgement emitted here. */
static esp_err_t fluid_ble_media_binary(const uint8_t *data, size_t data_size,
                                        char *response, size_t response_size)
{
    if (response == NULL || response_size == 0U) return ESP_ERR_INVALID_ARG;
    response[0] = '\0';
    if (!s_ble_media_transfer_active || s_ble_media_file == NULL) {
        snprintf(response, response_size, "ERR no active media transfer\r\n");
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || data_size <= BLE_MEDIA_BINARY_HEADER) {
        snprintf(response, response_size, "ERR MEDIA_BIN packet too small\r\n");
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t offset = ((uint32_t)data[0]) |
                            ((uint32_t)data[1] << 8) |
                            ((uint32_t)data[2] << 16) |
                            ((uint32_t)data[3] << 24);
    const size_t payload_size = data_size - BLE_MEDIA_BINARY_HEADER;
    if (offset != s_ble_media_received_size ||
        offset + payload_size > s_ble_media_expected_size) {
        snprintf(response, response_size, "ERR MEDIA_BIN expected=%u got=%u\r\n",
                 (unsigned)s_ble_media_received_size, (unsigned)offset);
        return ESP_ERR_INVALID_ARG;
    }
    if (fwrite(data + BLE_MEDIA_BINARY_HEADER, 1, payload_size,
               s_ble_media_file) != payload_size) {
        home_media_abort_ble_transfer(true);
        snprintf(response, response_size, "ERR media write failed\r\n");
        return ESP_FAIL;
    }
    s_ble_media_received_size += (uint32_t)payload_size;
    ++s_ble_media_binary_packets;
    const bool final_packet = s_ble_media_received_size == s_ble_media_expected_size;
    if (final_packet ||
        (s_ble_media_binary_packets % BLE_MEDIA_BINARY_ACK_EVERY) == 0U) {
        if (fflush(s_ble_media_file) != 0) {
            home_media_abort_ble_transfer(true);
            snprintf(response, response_size, "ERR media flush failed\r\n");
            return ESP_FAIL;
        }
        snprintf(response, response_size, "OK MEDIA_BIN %u/%u\r\n",
                 (unsigned)s_ble_media_received_size,
                 (unsigned)s_ble_media_expected_size);
    }
    return ESP_OK;
}

static esp_err_t fluid_ble_media_command(const char *text, char *response,
                                         size_t response_size)
{
    if (text == NULL || response == NULL) return ESP_ERR_INVALID_ARG;
    if (strcasecmp(text, "MEDIA_LIST") == 0) {
        if (s_ble_media_transfer_active || s_usb_msc_started) {
            ESP_LOGW(TAG, "BLE media: list rejected transfer=%d usb_msc=%d",
                     s_ble_media_transfer_active, s_usb_msc_started);
            snprintf(response, response_size, "ERR media busy\r\n");
            return ESP_ERR_INVALID_STATE;
        }
        const size_t count = ble_collect_media_entries(
            s_ble_media_command_entries,
            sizeof(s_ble_media_command_entries) / sizeof(s_ble_media_command_entries[0]));
        ESP_LOGI(TAG, "BLE media: list count=%u", (unsigned)count);
        snprintf(response, response_size, "OK MEDIA_LIST %u\r\n",
                 (unsigned)count);
        return ESP_OK;
    }
    if (strncasecmp(text, "MEDIA_INFO ", 11U) == 0) {
        char *end = NULL;
        const unsigned long index = strtoul(text + 11, &end, 10);
        while (end != NULL && *end != '\0' && isspace((unsigned char)*end)) ++end;
        if (end == NULL || *end != '\0' || index >= HOME_EXTERNAL_MEDIA_MAX) {
            snprintf(response, response_size, "ERR invalid media index\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        const size_t count = ble_collect_media_entries(
            s_ble_media_command_entries,
            sizeof(s_ble_media_command_entries) / sizeof(s_ble_media_command_entries[0]));
        if (index >= count) {
            snprintf(response, response_size, "ERR media index out of range\r\n");
            return ESP_ERR_NOT_FOUND;
        }
        const ble_media_entry_t *media = &s_ble_media_command_entries[index];
        snprintf(response, response_size,
                 "OK MEDIA_INFO %lu %s %u %u %u\r\n", index, media->name,
                 (unsigned)media->size, (unsigned)media->frames,
                 (unsigned)media->fps);
        return ESP_OK;
    }
    if (strncasecmp(text, "MEDIA_PLAY ", 11U) == 0) {
        char extra = '\0';
        s_ble_media_command_name[0] = '\0';
        if (s_ble_media_transfer_active || s_usb_msc_started ||
            s_home_media_rescan_pending ||
            sscanf(text + 11, "%63s %c", s_ble_media_command_name, &extra) != 1 ||
            !ble_media_name_valid(s_ble_media_command_name)) {
            snprintf(response, response_size, "ERR invalid media name\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        if (!home_media_uses_nand() || s_home_external_media_count == 0U) {
            snprintf(response, response_size, "ERR no external media\r\n");
            return ESP_ERR_NOT_FOUND;
        }
        uint8_t target = HOME_MEDIA_INVALID_INDEX;
        for (uint8_t index = 0; index < s_home_external_media_count; ++index) {
            if (strcmp(s_home_external_media[index].name,
                       s_ble_media_command_name) == 0) {
                target = index;
                break;
            }
        }
        if (target == HOME_MEDIA_INVALID_INDEX) {
            snprintf(response, response_size, "ERR media not found\r\n");
            return ESP_ERR_NOT_FOUND;
        }
        const uint8_t from = s_home_media_index;
        if (from != target) {
            s_home_pending_slide_from = from;
            s_home_pending_slide_to = target;
            s_home_pending_slide_from_frame = s_home_animation_frame;
            s_home_pending_slide_direction = target > from ? -1 : 1;
            s_home_slide_waiting = true;
        }
        snprintf(response, response_size, "OK MEDIA_PLAY %s %u\r\n",
                 s_ble_media_command_name, (unsigned)target);
        ESP_LOGI(TAG, "BLE media: play %s index=%u", s_ble_media_command_name,
                 (unsigned)target);
        return ESP_OK;
    }
    if (strncasecmp(text, "MEDIA_DELETE ", 13U) == 0) {
        char extra = '\0';
        s_ble_media_command_name[0] = '\0';
        if (s_ble_media_transfer_active || s_usb_msc_started ||
            sscanf(text + 13, "%63s %c", s_ble_media_command_name, &extra) != 1 ||
            !ble_media_name_valid(s_ble_media_command_name)) {
            snprintf(response, response_size, "ERR invalid media name\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        if (!s_sd_nand_mounted || !home_media_pause_reader()) {
            snprintf(response, response_size, "ERR media busy or NAND unavailable\r\n");
            return ESP_ERR_INVALID_STATE;
        }
        snprintf(s_ble_media_scan_path, sizeof(s_ble_media_scan_path),
                 "%s/media/%s", SD_NAND_MOUNT_POINT, s_ble_media_command_name);
        errno = 0;
        const int result = remove(s_ble_media_scan_path);
        const int remove_errno = errno;
        struct stat deleted_info;
        errno = 0;
        const int verify_result = result == 0 ?
                                  stat(s_ble_media_scan_path, &deleted_info) : -1;
        const int verify_errno = errno;
        home_media_resume_reader();
        if (result != 0) {
            snprintf(response, response_size, "%s\r\n",
                     remove_errno == ENOENT ? "ERR media not found" :
                                               "ERR media delete failed");
            return remove_errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
        }
        if (verify_result == 0 || verify_errno != ENOENT) {
            ESP_LOGE(TAG, "BLE media: delete verification failed for %s errno=%d",
                     s_ble_media_command_name, verify_errno);
            snprintf(response, response_size, "ERR media delete not committed\r\n");
            return ESP_FAIL;
        }
        s_home_media_rescan_pending = true;
        snprintf(response, response_size, "OK MEDIA_DELETED %s\r\n",
                 s_ble_media_command_name);
        ESP_LOGI(TAG, "BLE media: deleted %s", s_ble_media_command_name);
        return ESP_OK;
    }
    if (strncasecmp(text, "MEDIA_BEGIN ", 12U) == 0) {
        s_ble_media_command_name[0] = '\0';
        unsigned long long size = 0;
        unsigned long frames = 0;
        unsigned int fps = 0;
        char extra = '\0';
        if (s_ble_media_transfer_active || s_home_media_rescan_pending ||
            sscanf(text + 12, "%63s %llu %lu %u %c", s_ble_media_command_name,
                   &size, &frames, &fps, &extra) != 4 ||
            !ble_media_name_valid(s_ble_media_command_name) ||
            size == 0 || frames == 0 || frames > UINT32_MAX || fps < 1 || fps > 12 ||
            size != (unsigned long long)frames * LCD_FRAME_BYTES ||
            size > UINT32_MAX) {
            snprintf(response, response_size,
                     "ERR MEDIA_BEGIN name size frames fps; raw RGB565 %ux%u\r\n",
                     LCD_WIDTH, LCD_HEIGHT);
            return ESP_ERR_INVALID_ARG;
        }
        if (!s_sd_nand_mounted || s_usb_msc_started || !home_media_pause_reader()) {
            snprintf(response, response_size, "ERR media busy or NAND unavailable\r\n");
            return ESP_ERR_INVALID_STATE;
        }
        errno = 0;
        const int mkdir_result = mkdir("/sdnand/media", 0777);
        const int mkdir_errno = errno;
        struct stat media_directory_info;
        const int directory_stat_result = stat("/sdnand/media", &media_directory_info);
        if (mkdir_result != 0 && mkdir_errno != EEXIST) {
            ESP_LOGE(TAG, "BLE media: mkdir /sdnand/media failed errno=%d (%s)",
                     mkdir_errno, strerror(mkdir_errno));
            home_media_resume_reader();
            snprintf(response, response_size, "ERR cannot prepare media directory\r\n");
            return ESP_FAIL;
        }
        if (directory_stat_result != 0 || !S_ISDIR(media_directory_info.st_mode)) {
            const int directory_errno = errno;
            ESP_LOGE(TAG, "BLE media: /sdnand/media is not a directory stat=%d errno=%d (%s)",
                     directory_stat_result, directory_errno, strerror(directory_errno));
            home_media_resume_reader();
            snprintf(response, response_size, "ERR invalid media directory\r\n");
            return ESP_FAIL;
        }
        snprintf(s_ble_media_name, sizeof(s_ble_media_name), "%s",
                 s_ble_media_command_name);
        snprintf(s_ble_media_final_path, sizeof(s_ble_media_final_path),
                 "/sdnand/media/%s", s_ble_media_name);
        snprintf(s_ble_media_temp_path, sizeof(s_ble_media_temp_path),
                 "/sdnand/media/%s", BLE_MEDIA_TEMP_NAME);
        (void)remove(s_ble_media_temp_path);
        errno = 0;
        s_ble_media_file = fopen(s_ble_media_temp_path, "wb");
        if (s_ble_media_file == NULL) {
            const int open_errno = errno;
            ESP_LOGE(TAG, "BLE media: fopen('%s','wb') failed errno=%d (%s); "
                     "mounted=%d card=%p dir_stat=%d mode=0%o",
                     s_ble_media_temp_path, open_errno, strerror(open_errno),
                     s_sd_nand_mounted, (void *)s_sd_nand_card,
                     directory_stat_result, (unsigned)(media_directory_info.st_mode & 0777));
            home_media_resume_reader();
            snprintf(response, response_size, "ERR cannot open media temp file (%d)\r\n",
                     open_errno);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "BLE media: staging '%s' -> '%s'", s_ble_media_temp_path,
                 s_ble_media_final_path);
        s_ble_media_expected_size = (uint32_t)size;
        s_ble_media_expected_frames = (uint32_t)frames;
        s_ble_media_expected_fps = (uint8_t)fps;
        s_ble_media_received_size = 0;
        s_ble_media_binary_packets = 0;
        s_ble_media_transfer_active = true;
        ESP_LOGI(TAG, "BLE media: begin %s size=%u frames=%u fps=%u",
                 s_ble_media_name, (unsigned)s_ble_media_expected_size,
                 (unsigned)s_ble_media_expected_frames,
                 (unsigned)s_ble_media_expected_fps);
        snprintf(response, response_size, "OK MEDIA_READY %u\r\n",
                 (unsigned)s_ble_media_expected_size);
        return ESP_OK;
    }
    if (strcasecmp(text, "MEDIA_ABORT") == 0) {
        if (!s_ble_media_transfer_active) {
            snprintf(response, response_size, "OK MEDIA_IDLE\r\n");
            return ESP_OK;
        }
        home_media_abort_ble_transfer(true);
        snprintf(response, response_size, "OK MEDIA_ABORTED\r\n");
        return ESP_OK;
    }
    if (strncasecmp(text, "MEDIA_DATA ", 11U) == 0) {
        if (!s_ble_media_transfer_active || s_ble_media_file == NULL) {
            snprintf(response, response_size, "ERR no active media transfer\r\n");
            return ESP_ERR_INVALID_STATE;
        }
        snprintf(s_ble_media_command_fields, sizeof(s_ble_media_command_fields),
                 "%s", text + 11);
        char *separator = strchr(s_ble_media_command_fields, ' ');
        if (separator == NULL) {
            snprintf(response, response_size, "ERR MEDIA_DATA offset hex\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        *separator++ = '\0';
        char *end = NULL;
        const unsigned long offset = strtoul(s_ble_media_command_fields, &end, 10);
        const size_t hex_length = strlen(separator);
        if (end == s_ble_media_command_fields || *end != '\0' ||
            offset != s_ble_media_received_size ||
            hex_length == 0 || (hex_length & 1U) != 0 || hex_length > 216U ||
            offset + hex_length / 2U > s_ble_media_expected_size) {
            snprintf(response, response_size, "ERR expected offset=%u max_hex=216\r\n",
                     (unsigned)s_ble_media_received_size);
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t index = 0; index < hex_length / 2U; ++index) {
            const int high = ble_media_hex_nibble(separator[index * 2U]);
            const int low = ble_media_hex_nibble(separator[index * 2U + 1U]);
            if (high < 0 || low < 0) {
                snprintf(response, response_size, "ERR invalid hex payload\r\n");
                return ESP_ERR_INVALID_ARG;
            }
            s_ble_media_command_bytes[index] = (uint8_t)((high << 4) | low);
        }
        if (fwrite(s_ble_media_command_bytes, 1, hex_length / 2U,
                   s_ble_media_file) != hex_length / 2U ||
            fflush(s_ble_media_file) != 0) {
            home_media_abort_ble_transfer(true);
            snprintf(response, response_size, "ERR media write failed\r\n");
            return ESP_FAIL;
        }
        s_ble_media_received_size += (uint32_t)(hex_length / 2U);
        snprintf(response, response_size, "OK MEDIA_DATA %u/%u\r\n",
                 (unsigned)s_ble_media_received_size,
                 (unsigned)s_ble_media_expected_size);
        return ESP_OK;
    }
    if (strcasecmp(text, "MEDIA_END") == 0) {
        if (!s_ble_media_transfer_active || s_ble_media_file == NULL) {
            snprintf(response, response_size, "ERR no active media transfer\r\n");
            return ESP_ERR_INVALID_STATE;
        }
        if (s_ble_media_received_size != s_ble_media_expected_size) {
            snprintf(response, response_size, "ERR incomplete %u/%u\r\n",
                     (unsigned)s_ble_media_received_size,
                     (unsigned)s_ble_media_expected_size);
            return ESP_ERR_INVALID_STATE;
        }
        errno = 0;
        const int close_result = fclose(s_ble_media_file);
        const int close_errno = errno;
        s_ble_media_file = NULL;
        if (close_result != 0) {
            ESP_LOGE(TAG, "BLE media: fclose('%s') failed errno=%d (%s)",
                     s_ble_media_temp_path, close_errno, strerror(close_errno));
            home_media_abort_ble_transfer(true);
            snprintf(response, response_size, "ERR media close failed (%d)\r\n",
                     close_errno);
            return ESP_FAIL;
        }
        errno = 0;
        const int remove_result = remove(s_ble_media_final_path);
        const int remove_errno = errno;
        if (remove_result != 0 && remove_errno != ENOENT) {
            ESP_LOGE(TAG, "BLE media: remove('%s') failed errno=%d (%s)",
                     s_ble_media_final_path, remove_errno, strerror(remove_errno));
            home_media_abort_ble_transfer(true);
            snprintf(response, response_size, "ERR media target busy (%d)\r\n",
                     remove_errno);
            return ESP_FAIL;
        }
        errno = 0;
        if (rename(s_ble_media_temp_path, s_ble_media_final_path) != 0) {
            const int rename_errno = errno;
            ESP_LOGE(TAG, "BLE media: rename('%s' -> '%s') failed errno=%d (%s)",
                     s_ble_media_temp_path, s_ble_media_final_path,
                     rename_errno, strerror(rename_errno));
            home_media_abort_ble_transfer(true);
            snprintf(response, response_size, "ERR media rename failed (%d)\r\n",
                     rename_errno);
            return ESP_FAIL;
        }
        s_ble_media_transfer_active = false;
        s_home_media_rescan_pending = true;
        ESP_LOGI(TAG, "BLE media: completed %s (%u bytes)", s_ble_media_name,
                 (unsigned)s_ble_media_expected_size);
        snprintf(response, response_size, "OK MEDIA_COMMITTED %s\r\n", s_ble_media_name);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static bool ensure_home_media_runtime(void);

static void apply_home_media_rescan_if_pending(void)
{
    if (!s_home_media_rescan_pending) return;
    scan_home_external_media();
    for (int slot = 0; slot < HOME_MEDIA_CACHE_SLOTS; ++slot) {
        s_home_media_cache_index[slot] = HOME_MEDIA_INVALID_INDEX;
        s_home_media_cache_frame[slot] = UINT32_MAX;
    }
    if (home_media_uses_nand()) {
        /* The initial boot may have had an empty media directory.  In that
         * case init_home_external_media() intentionally did not allocate the
         * PSRAM cache or reader task; create them now for the first upload. */
        if (!ensure_home_media_runtime()) {
            ESP_LOGW(TAG, "BLE media: unable to start NAND reader after rescan");
            s_home_external_media_count = 0;
            s_home_media_io_enabled = false;
        }
    } else {
        s_home_media_io_enabled = false;
    }
    s_home_media_index = 0;
    s_home_animation_frame = 0;
    s_home_slide_active = false;
    s_home_slide_waiting = false;
    s_home_dirty = true;
    s_apps_canvas_valid = false;
    s_home_media_rescan_pending = false;
    home_media_resume_reader();
    ESP_LOGI(TAG, "BLE media: home carousel rescanned (%u item(s))",
             (unsigned)s_home_external_media_count);
}

static int home_find_cached_frame(uint8_t media_index, uint32_t frame)
{
    for (int slot = 0; slot < HOME_MEDIA_CACHE_SLOTS; ++slot) {
        if (s_home_media_cache[slot] != NULL &&
            s_home_media_cache_index[slot] == media_index &&
            s_home_media_cache_frame[slot] == frame) {
            return slot;
        }
    }
    return -1;
}

static bool home_cache_slot_is(uint8_t media_index, uint32_t frame,
                               uint8_t protected_index, uint32_t protected_frame)
{
    return protected_index != HOME_MEDIA_INVALID_INDEX &&
           media_index == protected_index && frame == protected_frame;
}

static int home_select_cache_slot(uint8_t protected_index_a, uint32_t protected_frame_a,
                                  uint8_t protected_index_b, uint32_t protected_frame_b)
{
    for (int slot = 0; slot < HOME_MEDIA_CACHE_SLOTS; ++slot) {
        if (s_home_media_cache[slot] != NULL &&
            !home_cache_slot_is(s_home_media_cache_index[slot],
                                s_home_media_cache_frame[slot],
                                protected_index_a, protected_frame_a) &&
            !home_cache_slot_is(s_home_media_cache_index[slot],
                                s_home_media_cache_frame[slot],
                                protected_index_b, protected_frame_b)) {
            return slot;
        }
    }
    return -1;
}

static bool home_ensure_external_frame(uint8_t media_index, uint32_t frame,
                                       uint8_t protected_index_a,
                                       uint32_t protected_frame_a,
                                       uint8_t protected_index_b,
                                       uint32_t protected_frame_b)
{
    if (!s_home_media_io_enabled || !home_media_uses_nand() ||
        media_index >= s_home_external_media_count) {
        return false;
    }
    const home_external_media_t *media = &s_home_external_media[media_index];
    if (media->frames == 0) {
        return false;
    }
    frame %= media->frames;
    if (home_find_cached_frame(media_index, frame) >= 0) {
        return true;
    }

    const int slot = home_select_cache_slot(protected_index_a, protected_frame_a,
                                            protected_index_b, protected_frame_b);
    if (slot < 0) {
        ESP_LOGE(TAG, "Media: no PSRAM frame cache for %s", media->name);
        return false;
    }
    const uint64_t offset = (uint64_t)frame * LCD_FRAME_BYTES;
    if (offset > 0x7fffffffULL) {
        ESP_LOGE(TAG, "Media: frame offset too large for FAT seek: %s", media->name);
        return false;
    }

    /* Make the slot invisible to the renderer before its contents change.
     * The selected slot is never either currently displayed frame. */
    s_home_media_cache_index[slot] = HOME_MEDIA_INVALID_INDEX;
    s_home_media_cache_frame[slot] = UINT32_MAX;

    const int64_t read_start_us = esp_timer_get_time();
    FILE *file = fopen(media->path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Media: open failed: %s", media->path);
        return false;
    }
    const int seek_result = fseek(file, (long)offset, SEEK_SET);
    size_t received = 0;
    while (seek_result == 0 && received < LCD_FRAME_BYTES && s_home_media_io_enabled) {
        const size_t request = LCD_FRAME_BYTES - received > 8192 ? 8192 :
                               LCD_FRAME_BYTES - received;
        const size_t chunk = fread((uint8_t *)s_home_media_cache[slot] + received,
                                   1, request, file);
        received += chunk;
        if (chunk != request) {
            break;
        }
        /* A full U2 frame takes roughly 200 ms.  Let the higher-priority UI
         * task run between small reads so a swipe is never lost to NAND I/O. */
        taskYIELD();
    }
    fclose(file);
    if (received != LCD_FRAME_BYTES) {
        ESP_LOGE(TAG, "Media: frame %u read failed for %s (%u/%u B)",
                 (unsigned)frame, media->name, (unsigned)received,
                 (unsigned)LCD_FRAME_BYTES);
        return false;
    }

    s_home_perf_read_us += (uint64_t)(esp_timer_get_time() - read_start_us);
    ++s_home_perf_reads;

    s_home_media_cache_index[slot] = media_index;
    s_home_media_cache_frame[slot] = frame;
    return true;
}

static bool home_request_external_frame(uint8_t media_index, uint32_t frame)
{
    if (!home_media_uses_nand() || media_index >= s_home_external_media_count) {
        return false;
    }
    frame %= s_home_external_media[media_index].frames;
    if (home_find_cached_frame(media_index, frame) >= 0) {
        return true;
    }
    if (s_home_media_request_queue == NULL || !s_home_media_io_enabled) {
        return false;
    }
    if ((s_home_media_reader_busy && s_home_media_loading_index == media_index &&
         s_home_media_loading_frame == frame) ||
        (s_home_media_queued_index == media_index &&
         s_home_media_queued_frame == frame)) {
        return true;
    }

    const home_media_request_t request = {
        .media_index = media_index,
        .frame = frame,
    };
    if (xQueueOverwrite(s_home_media_request_queue, &request) != pdPASS) {
        ESP_LOGW(TAG, "[DEBUG-HOME-ASYNC-D4C1] unable to queue %u:%u",
                 (unsigned)media_index, (unsigned)frame);
        return false;
    }
    s_home_media_queued_index = media_index;
    s_home_media_queued_frame = frame;
    ESP_LOGI(TAG, "[DEBUG-HOME-ASYNC-D4C1] queued %u:%u",
             (unsigned)media_index, (unsigned)frame);
    return true;
}

static void home_media_reader_task(void *argument)
{
    (void)argument;
    home_media_request_t request;
    while (true) {
        if (xQueueReceive(s_home_media_request_queue, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }
        if (request.media_index == s_home_media_queued_index &&
            request.frame == s_home_media_queued_frame) {
            s_home_media_queued_index = HOME_MEDIA_INVALID_INDEX;
            s_home_media_queued_frame = UINT32_MAX;
        }
        if (!s_home_media_io_enabled ||
            home_find_cached_frame(request.media_index, request.frame) >= 0) {
            continue;
        }

        s_home_media_reader_busy = true;
        s_home_media_loading_index = request.media_index;
        s_home_media_loading_frame = request.frame;
        const uint8_t protected_index_a = s_home_media_index;
        const uint32_t protected_frame_a = s_home_animation_frame;
        const uint8_t protected_index_b = s_home_slide_active ?
            s_home_slide_from_index : HOME_MEDIA_INVALID_INDEX;
        const uint32_t protected_frame_b = s_home_slide_from_frame;
        const bool loaded = home_ensure_external_frame(request.media_index, request.frame,
                                                        protected_index_a,
                                                        protected_frame_a,
                                                        protected_index_b,
                                                        protected_frame_b);
        s_home_media_reader_busy = false;
        s_home_media_loading_index = HOME_MEDIA_INVALID_INDEX;
        s_home_media_loading_frame = UINT32_MAX;
        ESP_LOGI(TAG, "[DEBUG-HOME-ASYNC-D4C1] %s %u:%u",
                 loaded ? "ready" : "failed",
                 (unsigned)request.media_index, (unsigned)request.frame);
    }
}

static void home_record_render_time(int64_t render_start_us, uint64_t compose_us,
                                    uint64_t dma_us, const char *kind)
{
    const uint32_t elapsed_us = (uint32_t)(esp_timer_get_time() - render_start_us);
    s_home_perf_render_us += elapsed_us;
    s_home_perf_compose_us += compose_us;
    s_home_perf_dma_us += dma_us;
    ++s_home_perf_renders;
    if (elapsed_us > s_home_perf_render_max_us) {
        s_home_perf_render_max_us = elapsed_us;
    }
    if ((s_home_perf_renders % 8U) == 0U) {
        ESP_LOGI(TAG, "[DEBUG-HOME-PERF-8D2E] %s n=%u render=%uus avg=%lluus "
                 "compose=%lluus dma=%lluus max=%uus read=%u avg=%lluus",
                 kind, (unsigned)s_home_perf_renders, (unsigned)elapsed_us,
                 s_home_perf_render_us / s_home_perf_renders,
                 s_home_perf_compose_us / s_home_perf_renders,
                 s_home_perf_dma_us / s_home_perf_renders,
                 (unsigned)s_home_perf_render_max_us, (unsigned)s_home_perf_reads,
                 s_home_perf_reads == 0 ? 0ULL :
                     s_home_perf_read_us / s_home_perf_reads);
    }
}

static bool ensure_home_media_runtime(void)
{
    if (!home_media_uses_nand()) {
        return true;
    }

    s_home_media_io_enabled = true;
    for (int slot = 0; slot < HOME_MEDIA_CACHE_SLOTS; ++slot) {
        if (s_home_media_cache[slot] == NULL) {
            s_home_media_cache[slot] = heap_caps_aligned_calloc(
                64, 1, LCD_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (s_home_media_cache[slot] == NULL) {
            ESP_LOGE(TAG, "Media: PSRAM allocation failed for cache %d", slot);
            s_home_external_media_count = 0;
            return false;
        }
        s_home_media_cache_index[slot] = HOME_MEDIA_INVALID_INDEX;
        s_home_media_cache_frame[slot] = UINT32_MAX;
    }
    if (!home_ensure_external_frame(0, 0, HOME_MEDIA_INVALID_INDEX, UINT32_MAX,
                                    HOME_MEDIA_INVALID_INDEX, UINT32_MAX)) {
        s_home_external_media_count = 0;
        return false;
    }
    s_home_animation_frame = 0;
    const home_external_media_t *first = &s_home_external_media[0];
    s_home_next_animation_tick = xTaskGetTickCount() +
        pdMS_TO_TICKS(first->frames > 1 ? 1000 / first->fps : HOME_REFRESH_MS);
    if (s_home_media_request_queue == NULL) {
        s_home_media_request_queue = xQueueCreate(1, sizeof(home_media_request_t));
    }
    if (s_home_media_request_queue == NULL) {
        ESP_LOGE(TAG, "Media: background reader unavailable; using only first frame");
        return true;
    }
    if (!s_home_media_reader_started) {
        if (xTaskCreatePinnedToCore(home_media_reader_task, "home_media", 4096, NULL,
                                    tskIDLE_PRIORITY + 1, NULL, 0) != pdPASS) {
            ESP_LOGE(TAG, "Media: background reader task unavailable; using first frame");
            vQueueDelete(s_home_media_request_queue);
            s_home_media_request_queue = NULL;
            return true;
        }
        s_home_media_reader_started = true;
    }
    if (first->frames > 1) {
        (void)home_request_external_frame(0, 1);
    }
    return true;
}

static bool init_home_external_media(void)
{
    scan_home_external_media();
    return ensure_home_media_runtime();
}

static void home_reset_animation_clock(TickType_t now);

static bool home_prepare_media_transition(uint8_t from, uint8_t to)
{
    if (!home_media_uses_nand()) {
        return true;
    }
    const bool from_ready = home_find_cached_frame(from, s_home_animation_frame) >= 0;
    const bool to_ready = from == to || home_find_cached_frame(to, 0) >= 0;
    if (!from_ready) {
        (void)home_request_external_frame(from, s_home_animation_frame);
    }
    if (!to_ready) {
        (void)home_request_external_frame(to, 0);
    }
    return from_ready && to_ready;
}

static bool home_begin_pending_media_transition(TickType_t now)
{
    if (!s_home_slide_waiting) {
        return false;
    }
    if (!home_prepare_media_transition(s_home_pending_slide_from,
                                       s_home_pending_slide_to)) {
        return false;
    }

    s_home_slide_from_index = s_home_pending_slide_from;
    s_home_slide_from_frame = s_home_pending_slide_from_frame;
    s_home_slide_direction = s_home_pending_slide_direction;
    s_home_media_index = s_home_pending_slide_to;
    home_reset_animation_clock(now);
    s_home_slide_step = 1;
    s_home_slide_active = true;
    s_home_slide_waiting = false;
    s_home_dirty = false;
    ESP_LOGI(TAG, "Media: slide %s to item %u/%u%s",
             s_home_slide_direction < 0 ? "left" : "right",
             (unsigned)(s_home_media_index + 1), (unsigned)home_media_count(),
             home_media_uses_nand() ? " (NAND, preloaded)" : " (embedded)");
    return true;
}

static void home_reset_animation_clock(TickType_t now)
{
    s_home_animation_frame = 0;
    if (!home_media_uses_nand() || s_home_media_index >= s_home_external_media_count) {
        return;
    }
    const home_external_media_t *media = &s_home_external_media[s_home_media_index];
    s_home_next_animation_tick = now +
        pdMS_TO_TICKS(media->frames > 1 ? 1000 / media->fps : HOME_REFRESH_MS);
}

static bool home_advance_animation(TickType_t now)
{
    if (!home_media_uses_nand() || s_home_slide_active || s_home_slide_waiting ||
        s_home_media_index >= s_home_external_media_count) {
        return false;
    }
    const home_external_media_t *media = &s_home_external_media[s_home_media_index];
    if (media->frames <= 1 || (int32_t)(now - s_home_next_animation_tick) < 0) {
        return false;
    }

    const uint32_t next_frame = (s_home_animation_frame + 1) % media->frames;
    if (home_find_cached_frame(s_home_media_index, next_frame) < 0) {
        (void)home_request_external_frame(s_home_media_index, next_frame);
        s_home_next_animation_tick = now + pdMS_TO_TICKS(20);
        return false;
    }
    s_home_animation_frame = next_frame;
    s_home_next_animation_tick = now + pdMS_TO_TICKS(1000 / media->fps);
    return true;
}

/* Move static OLED content by a couple of pixels over time.  Clamp the source
 * coordinates instead of exposing a black edge through the round aperture. */
static uint16_t home_cached_pixel(const uint16_t *frame, int x, int y)
{
    const int source_x = clamp_int(x - s_home_media_shift_x, 0, LCD_WIDTH - 1);
    const int source_y = clamp_int(y - s_home_media_shift_y, 0, LCD_HEIGHT - 1);
    return frame[(size_t)source_y * LCD_WIDTH + (size_t)source_x];
}

static void home_update_burnin_offsets(TickType_t now)
{
    static const int8_t kOffsets[][2] = {
        {0, 0}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1},
    };

    if (s_ui_screen != UI_SCREEN_HOME || s_home_slide_active || s_home_slide_waiting) {
        return;
    }
    if ((int32_t)(now - s_next_home_status_shift) >= 0) {
        s_home_status_shift_phase = (uint8_t)((s_home_status_shift_phase + 1U) %
                                              (sizeof(kOffsets) / sizeof(kOffsets[0])));
        s_home_status_shift_x = kOffsets[s_home_status_shift_phase][0];
        s_home_status_shift_y = kOffsets[s_home_status_shift_phase][1];
        s_home_dirty = true;
        s_next_home_status_shift = now + pdMS_TO_TICKS(HOME_STATUS_SHIFT_MS);
    }

    /* Animated media already distributes its OLED load.  Only shift an
     * actual still image (or the embedded fallback) as a whole. */
    const bool animated = home_media_uses_nand() &&
                          s_home_media_index < s_home_external_media_count &&
                          s_home_external_media[s_home_media_index].frames > 1;
    if (!animated && (int32_t)(now - s_next_home_pixel_shift) >= 0) {
        s_home_media_shift_phase = (uint8_t)((s_home_media_shift_phase + 1U) %
                                             (sizeof(kOffsets) / sizeof(kOffsets[0])));
        s_home_media_shift_x = kOffsets[s_home_media_shift_phase][0];
        s_home_media_shift_y = kOffsets[s_home_media_shift_phase][1];
        s_home_dirty = true;
        s_next_home_pixel_shift = now + pdMS_TO_TICKS(HOME_PIXEL_SHIFT_MS);
    }
}

static uint16_t home_hatsune_background_pixel(int x, int y)
{
    /* A quiet Miku-teal fallback page: it remains attractive on an empty
     * card without pretending to be a missing photo, and keeps the battery
     * overlay readable over a deliberately dark upper arc. */
    const int dx = x - LCD_WIDTH / 2;
    const int dy = y - LCD_HEIGHT / 2;
    const int radius2 = dx * dx + dy * dy;
    const int halo = clamp_int(180 - radius2 / 670, 0, 180);
    uint16_t colour = rgb565(2 + halo / 34, 15 + halo / 6, 24 + halo / 5);

    /* Sparse cyan synthesiser grid, concentrated below the status area. */
    if (y > 88 && (((x + 12) % 44 == 0) || ((y + 9) % 44 == 0))) {
        colour = rgb565_blend(colour, rgb565(39, 197, 187), 78);
    }
    const int ribbon = (x * 3 + y * 2 + 37) % 173;
    if (ribbon < 3 && y > 96) {
        colour = rgb565_blend(colour, rgb565(85, 255, 230), 150);
    }
    const int glow_x = x - 345;
    const int glow_y = y - 292;
    const int glow = clamp_int(255 - (glow_x * glow_x + glow_y * glow_y) / 95, 0, 255);
    if (glow > 0) {
        colour = rgb565_blend(colour, rgb565(82, 255, 225), (uint8_t)(glow / 2));
    }
    return colour;
}

static const uint16_t *home_cached_frame(uint8_t index, uint32_t frame_number)
{
    if (!home_media_uses_nand()) {
        return NULL;
    }
    const int slot = home_find_cached_frame(index, frame_number);
    return slot >= 0 ? s_home_media_cache[slot] : NULL;
}

static const uint16_t *home_cached_frame_for_index(uint8_t index)
{
    if (!home_media_uses_nand()) {
        return NULL;
    }
    const uint32_t expected_frame = index == s_home_media_index ? s_home_animation_frame :
        (s_home_slide_active && index == s_home_slide_from_index ?
         s_home_slide_from_frame : 0);
    const uint16_t *exact = home_cached_frame(index, expected_frame);
    if (exact != NULL) {
        return exact;
    }
    for (int slot = 0; slot < HOME_MEDIA_CACHE_SLOTS; ++slot) {
        if (s_home_media_cache[slot] != NULL &&
            s_home_media_cache_index[slot] == index) {
            return s_home_media_cache[slot];
        }
    }
    return NULL;
}

static uint16_t home_background_pixel_for_index(uint8_t index, int x, int y)
{
    if (home_media_uses_nand()) {
        const uint16_t *frame = home_cached_frame_for_index(index);
        return frame != NULL ? home_cached_pixel(frame, x, y) : 0;
    }

    (void)index;
    return home_hatsune_background_pixel(x - s_home_media_shift_x,
                                         y - s_home_media_shift_y);
}

static uint16_t home_pixel_from_background(int x, int y, uint16_t colour)
{
    const int center_x = LCD_WIDTH / 2;
    const int center_y = LCD_HEIGHT / 2;
    if (!home_in_circle(x, y, center_x, center_y, 230)) {
        return 0x0000;
    }

    /* This RGB565LE still was subject-cropped specifically for the circular
     * panel.  The video counterpart stays external because its 35 MB of raw
     * frames belongs on NAND rather than in program flash. */
    /* Keep the overlay phone-like but deliberately compact: it should report
     * state without covering the subject.  Its colour, rather than a custom
     * lightning glyph, distinguishes charging and avoids glyph artifacts. */
    const int battery_left = 197 + s_home_status_shift_x;
    const int battery_right = 279 + s_home_status_shift_x;
    const int battery_top = 24 + s_home_status_shift_y;
    const int battery_bottom = 51 + s_home_status_shift_y;
    const bool in_battery = home_in_round_rect(x, y, battery_left, battery_top,
                                               battery_right, battery_bottom, 8);
    const bool in_inner = home_in_round_rect(x, y, battery_left + 3, battery_top + 3,
                                             battery_right - 3, battery_bottom - 3, 5);
    const uint16_t battery_colour = s_battery_charging ? rgb565(102, 246, 172) :
                                                          rgb565(238, 247, 255);
    if (in_battery && !in_inner) {
        return battery_colour;
    }
    if (x >= battery_right + 1 && x <= battery_right + 5 &&
        y >= 33 + s_home_status_shift_y && y <= 42 + s_home_status_shift_y) {
        return battery_colour;
    }
    if (in_inner) {
        const int fill_width = s_battery_percent_valid ?
                               clamp_int((int)s_battery_percent, 0, 100) *
                               (battery_right - battery_left - 6) / 100 : 0;
        if (x < battery_left + 4 + fill_width) {
            colour = rgb565_blend(colour, s_battery_charging ? rgb565(31, 157, 97) :
                                                         rgb565(73, 106, 132), 180);
        } else {
            colour = rgb565_blend(colour, rgb565(4, 14, 27), 190);
        }
    }
    const int percent_origin = (s_battery_percent_valid && s_battery_percent >= 100 ? 216 : 222) +
                               s_home_status_shift_x;
    uint8_t text_alpha = home_font_alpha(x, y, percent_origin, 30 + s_home_status_shift_y,
                                         &lv_font_montserrat_14, s_home_battery);
    if (text_alpha != 0) {
        return rgb565_blend(colour, battery_colour, text_alpha);
    }
    return colour;
}

static uint16_t home_pixel(int x, int y)
{
    return home_pixel_from_background(
        x, y, home_background_pixel_for_index(s_home_media_index, x, y));
}

static uint16_t home_slide_pixel(int x, int y)
{
    const int shift = ((int)s_home_slide_step * LCD_WIDTH) / HOME_SLIDE_STEPS;
    int source_x;
    uint8_t image_index;
    if (s_home_slide_direction < 0) {
        /* Finger moved left: old page leaves left, next page enters right. */
        source_x = x + shift;
        if (source_x < LCD_WIDTH) {
            image_index = s_home_slide_from_index;
        } else {
            source_x -= LCD_WIDTH;
            image_index = s_home_media_index;
        }
    } else {
        /* Finger moved right: old page leaves right, previous enters left. */
        source_x = x - shift;
        if (source_x >= 0) {
            image_index = s_home_slide_from_index;
        } else {
            source_x += LCD_WIDTH;
            image_index = s_home_media_index;
        }
    }
    return home_pixel_from_background(
        x, y, home_background_pixel_for_index(image_index, source_x, y));
}

static bool home_circle_bounds_for_y(int y, int *left, int *right)
{
    const int dy = y - LCD_HEIGHT / 2;
    const int radius_squared = 230 * 230 - dy * dy;
    if (radius_squared < 0) {
        return false;
    }
    const int span = (int)sqrtf((float)radius_squared);
    *left = clamp_int(LCD_WIDTH / 2 - span, 0, LCD_WIDTH - 1);
    *right = clamp_int(LCD_WIDTH / 2 + span, 0, LCD_WIDTH - 1);
    return *left <= *right;
}

/* External media is already a full native-resolution RGB565 frame in PSRAM.
 * Do not run the full home-pixel scene graph for all 221,350 pixels.  The
 * circle mask is one calculation per row and the battery overlay exists only
 * in rows 24..51, leaving the rest as a direct source-to-DMA conversion. */
static void home_compose_cached_line(uint16_t *line, const uint16_t *source, int y)
{
    int left = 0;
    int right = -1;
    if (!home_circle_bounds_for_y(y, &left, &right)) {
        memset(line, 0, LCD_WIDTH * sizeof(*line));
        return;
    }
    memset(line, 0, (size_t)left * sizeof(*line));
    const bool has_status_overlay = y >= 22 && y <= 53;
    if (has_status_overlay) {
        for (int x = left; x <= right; ++x) {
            line[x] = rgb565_for_sh8601(home_pixel_from_background(
                x, y, home_cached_pixel(source, x, y)));
        }
    } else {
        for (int x = left; x <= right; ++x) {
            line[x] = rgb565_for_sh8601(home_cached_pixel(source, x, y));
        }
    }
    memset(line + right + 1, 0, (size_t)(LCD_WIDTH - right - 1) * sizeof(*line));
}

static void home_compose_cached_slide_line(uint16_t *line, const uint16_t *from,
                                           const uint16_t *to, int y, int shift,
                                           int8_t direction)
{
    int left = 0;
    int right = -1;
    if (!home_circle_bounds_for_y(y, &left, &right)) {
        memset(line, 0, LCD_WIDTH * sizeof(*line));
        return;
    }
    memset(line, 0, (size_t)left * sizeof(*line));
    const bool has_status_overlay = y >= 22 && y <= 53;
    for (int x = left; x <= right; ++x) {
        int source_x = direction < 0 ? x + shift : x - shift;
        const uint16_t *source = from;
        if (source_x < 0) {
            source_x += LCD_WIDTH;
            source = to;
        } else if (source_x >= LCD_WIDTH) {
            source_x -= LCD_WIDTH;
            source = to;
        }
        uint16_t colour = home_cached_pixel(source, source_x, y);
        if (has_status_overlay) {
            colour = home_pixel_from_background(x, y, colour);
        }
        line[x] = rgb565_for_sh8601(colour);
    }
    memset(line + right + 1, 0, (size_t)(LCD_WIDTH - right - 1) * sizeof(*line));
}

/* watchOS-inspired honeycomb launcher.  PACON keeps large, separated
 * touch regions, but the visible icons are circular, layered and label-free
 * so the round panel reads like a watch launcher instead of a phone grid. */
static uint16_t apps_pixel(int x, int y)
{
    const int center_x = LCD_WIDTH / 2;
    const int center_y = LCD_HEIGHT / 2;
    if (!home_in_circle(x, y, center_x, center_y, 230)) {
        return 0;
    }
    const int dx = x - center_x;
    const int dy = y - center_y;
    const int glow = clamp_int(150 - (dx * dx + dy * dy) / 370, 0, 150);
    uint16_t colour = rgb565(glow / 45, glow / 30, glow / 18);
    /* Equal 3-3-1 honeycomb: every app owns the same 78 px circle and the
     * same 88 px touch target. */
    const int icon_size = 82;
    const int icon_radius = 39;
    const int fluid_cx = 142;
    const int ouo_cx = 333;
    const int sky_cx = 142;
    const int disk_cx = 333;
    const int top_cy = 145;
    const int bottom_cy = 265;
    const int watch_cx = LCD_WIDTH / 2;
    const int watch_cy = top_cy;
    const int watch_radius = icon_radius;
    const int settings_cx = LCD_WIDTH / 2;
    const int settings_cy = bottom_cy;

    if (home_in_circle(x, y, fluid_cx, top_cy, icon_radius)) {
        const int ix = x - fluid_cx;
        const int iy = y - top_cy;
        int shine = clamp_int(210 - (ix + 18) * (ix + 18) / 8 -
                                    (iy + 20) * (iy + 20) / 8, 0, 210);
        colour = rgb565(8 + shine / 16, 102 + shine / 3, 210 + shine / 5);
        const int blob_a = (ix + 16) * (ix + 16) + (iy + 5) * (iy + 5);
        const int blob_b = (ix - 15) * (ix - 15) + (iy - 13) * (iy - 13);
        const int blob_c = (ix - 5) * (ix - 5) + (iy + 20) * (iy + 20);
        if (blob_a < 440 || blob_b < 340 || blob_c < 200) {
            colour = rgb565(225, 252, 255);
        }
        if (ix * ix + iy * iy >= 35 * 35) colour = rgb565(64, 214, 255);
    }
    if (home_in_circle(x, y, ouo_cx, top_cy, icon_radius)) {
        const int ix = x - ouo_cx;
        const int iy = y - top_cy;
        /* 0u0 launcher icon: official OuO-inspired face.
         * The Android icon is deliberately minimal: a black field, two round
         * white eyes on a diagonal, and a lower-left white crescent.  Keep the
         * same proportions here instead of inventing a random in-app face. */
        const int radius2 = ix * ix + iy * iy;
        if (radius2 >= 35 * 35) {
            colour = rgb565(191, 90, 242);
        } else {
            colour = rgb565(1, 1, 2);
        }

        const int eye_a = (ix + 19) * (ix + 19) + (iy + 19) * (iy + 19);
        const int eye_b = (ix - 19) * (ix - 19) + (iy - 19) * (iy - 19);
        if (eye_a <= 9 * 9 || eye_b <= 9 * 9) {
            colour = rgb565(255, 255, 255);
        }

        const int mouth_outer = (ix + 2) * (ix + 2) + (iy - 4) * (iy - 4);
        const int mouth_cutout = (ix - 5) * (ix - 5) + (iy + 2) * (iy + 2);
        if (mouth_outer <= 14 * 14 && mouth_cutout > 13 * 13) {
            colour = rgb565(255, 255, 255);
        }
    }
    if (home_in_circle(x, y, watch_cx, watch_cy, watch_radius)) {
        const int ix = x - watch_cx;
        const int iy = y - watch_cy;
        const int radius2 = ix * ix + iy * iy;
        colour = radius2 >= 35 * 35 ? rgb565(255, 196, 74) :
                 radius2 > 30 * 30 ? rgb565(174, 119, 36) :
                 radius2 > 26 * 26 ? rgb565(241, 196, 91) :
                                     rgb565(25, 18, 13);
        const float angle = atan2f((float)iy, (float)ix);
        const float tick = fabsf(fmodf(angle + 6.28318531f, 0.52359878f));
        if (radius2 >= 22 * 22 && radius2 <= 27 * 27 &&
            (tick < 0.055f || tick > 0.468f)) {
            colour = rgb565(255, 224, 139);
        }
        /* Compact 10:10 hands, readable without a text label. */
        if ((iy >= -19 && iy <= 2 && abs(ix + iy / 2) <= 2) ||
            (ix >= -16 && ix <= 1 && abs(iy - ix / 3) <= 2) ||
            radius2 <= 4 * 4) {
            colour = rgb565(250, 244, 220);
        }
    }
    if (home_in_circle(x, y, sky_cx, bottom_cy, icon_radius)) {
        const int ix = x - sky_cx;
        const int iy = y - bottom_cy;
        colour = rgb565(4, 41, 43);
        const int radius2 = ix * ix + iy * iy;
        if ((radius2 > 25 * 25 - 44 && radius2 < 25 * 25 + 44) ||
            (radius2 > 16 * 16 - 30 && radius2 < 16 * 16 + 30) ||
            (ix >= -1 && ix <= 1) || (iy >= -1 && iy <= 1)) {
            colour = rgb565(69, 235, 165);
        }
        if (ix >= -3 && ix <= 3 && iy >= -20 && iy <= 6 && ix + iy / 4 >= -4) {
            colour = rgb565(255, 80, 100);
        }
        if (ix >= -8 && ix <= 8 && iy >= 2 && iy <= 8 &&
            (ix < -2 || ix > 2)) {
            colour = rgb565(255, 80, 100);
        }
        if (radius2 >= 35 * 35) colour = rgb565(69, 235, 165);
    }
    if (home_in_circle(x, y, disk_cx, bottom_cy, icon_radius)) {
        const int ix = x - (disk_cx - icon_size / 2);
        const int iy = y - (bottom_cy - icon_size / 2);
        colour = rgb565(53, 58, 72);
        if ((x - (disk_cx - 16)) * (x - (disk_cx - 16)) +
            (y - (bottom_cy - 20)) * (y - (bottom_cy - 20)) < 24 * 24) {
            colour = rgb565(75, 82, 100);
        }
        const bool drive_shell = home_in_round_rect(ix, iy, 13, 22, 69, 62, 9);
        const bool drive_face = home_in_round_rect(ix, iy, 17, 27, 65, 58, 6);
        if (drive_shell && !drive_face) {
            colour = rgb565(238, 244, 249);
        } else if (drive_face) {
            colour = rgb565(25, 29, 38);
            if ((ix >= 24 && ix <= 56 && iy >= 39 && iy <= 42) ||
                (ix - 58) * (ix - 58) + (iy - 50) * (iy - 50) <= 3 * 3) {
                colour = rgb565(50, 215, 75);
            }
        }
        if ((x - disk_cx) * (x - disk_cx) +
            (y - bottom_cy) * (y - bottom_cy) >= 35 * 35) {
            colour = rgb565(90, 200, 250);
        }
    }
    if (home_in_circle(x, y, settings_cx, settings_cy, icon_radius)) {
        const int ix = x - settings_cx;
        const int iy = y - settings_cy;
        const int radius2 = ix * ix + iy * iy;
        colour = rgb565(34, 38, 49);
        if (radius2 >= 12 * 12 && radius2 <= 19 * 19) {
            colour = rgb565(215, 220, 229);
        }
        if ((abs(ix) <= 5 && abs(iy) >= 16 && abs(iy) <= 21) ||
            (abs(iy) <= 5 && abs(ix) >= 16 && abs(ix) <= 21) ||
            (abs(ix - iy) <= 4 && abs(ix) >= 12 && abs(ix) <= 17) ||
            (abs(ix + iy) <= 4 && abs(ix) >= 12 && abs(ix) <= 17)) {
            colour = rgb565(215, 220, 229);
        }
        if (radius2 <= 7 * 7) {
            colour = rgb565(34, 38, 49);
        }
        if (radius2 >= 35 * 35) colour = rgb565(215, 220, 229);
    }
    /* Camera remote: a compact lens icon below Settings.  Its ring mirrors
     * the live HID readiness used by the dedicated shutter page. */
    const int camera_cx = LCD_WIDTH / 2;
    const int camera_cy = 385;
    const int camera_dx = x - camera_cx;
    const int camera_dy = y - camera_cy;
    const int camera_r2 = camera_dx * camera_dx + camera_dy * camera_dy;
    if (camera_r2 <= icon_radius * icon_radius) {
        const bool ready = ble_pacon_is_enabled() &&
                           ble_pacon_is_camera_remote_ready();
        colour = camera_r2 >= 35 * 35 ?
                 (ready ? rgb565(48, 209, 88) : rgb565(255, 159, 10)) :
                 rgb565(25, 27, 34);
        if (home_in_round_rect(camera_dx + 39, camera_dy + 39,
                               20, 27, 58, 52, 6) ||
            home_in_round_rect(camera_dx + 39, camera_dy + 39,
                               29, 22, 49, 30, 4)) {
            colour = rgb565(238, 242, 247);
        }
        if (camera_r2 <= 9 * 9) colour = rgb565(25, 27, 34);
    }
    /* Small page/home pill, matching the unobtrusive watch navigation cue. */
    if (home_in_round_rect(x, y, 210, 432, 265, 438, 3)) {
        return rgb565(112, 118, 132);
    }
    return colour;
}

static uint16_t apps_cached_pixel(int x, int y)
{
    if (s_lcd_canvas == NULL || !s_apps_canvas_valid) {
        return apps_pixel(x, y);
    }
    return s_lcd_canvas[(size_t)y * LCD_WIDTH + (size_t)x];
}

static uint16_t usb_disk_pixel(int x, int y)
{
    const int center_x = LCD_WIDTH / 2;
    const int center_y = LCD_HEIGHT / 2;
    if (!home_in_circle(x, y, center_x, center_y, 230)) {
        return 0;
    }

    uint16_t colour = rgb565(0, 0, 0);
    const bool ready = s_usb_msc_started || s_usb_msc_ui_on;
    const bool failed = !ready && s_usb_msc_result != ESP_ERR_INVALID_STATE;
    const uint16_t accent = failed ? rgb565(255, 69, 58) :
                            ready ? rgb565(48, 209, 88) : rgb565(10, 132, 255);
    const uint16_t text = rgb565(245, 245, 247);
    const uint16_t secondary = rgb565(174, 174, 178);

    /* A single circular status icon is more glanceable than the previous
     * instrument panel and follows the round watch icon vocabulary. */
    const int icon_dx = x - center_x;
    const int icon_dy = y - 205;
    const int icon_d2 = icon_dx * icon_dx + icon_dy * icon_dy;
    if (icon_d2 <= 78 * 78) {
        colour = rgb565(28, 28, 30);
        if (icon_d2 >= 73 * 73) {
            colour = accent;
        }
        const bool shell = home_in_round_rect(x, y, 174, 177, 301, 242, 15);
        const bool inner = home_in_round_rect(x, y, 181, 184, 294, 235, 10);
        if (shell && !inner) {
            colour = text;
        } else if (inner) {
            colour = rgb565(44, 44, 46);
            if (x >= 199 && x <= 263 && y >= 215 && y <= 220) {
                colour = accent;
            }
            if ((x - 276) * (x - 276) + (y - 217) * (y - 217) <= 6 * 6) {
                colour = accent;
            }
        }
    }

    const char *state = ready ? "USB DISK ON" : failed ? "USB ERROR" : "USB DISK OFF";
    const int state_x = ready ? 145 : failed ? 157 : 137;
    uint8_t alpha = home_font_alpha(x, y, state_x, 295, &lv_font_montserrat_28, state);
    if (alpha != 0) {
        return rgb565_blend(colour, text, alpha);
    }
    const char *hint = ready ? "EJECT ON COMPUTER BEFORE OFF" :
                       failed ? "TOGGLE TO TRY AGAIN" : "SERIAL REMAINS CONNECTED";
    const int hint_x = ready ? 105 : failed ? 151 : 137;
    alpha = home_font_alpha(x, y, hint_x, 335, &lv_font_montserrat_14, hint);
    if (alpha != 0) {
        return rgb565_blend(colour, secondary, alpha);
    }
    const bool track = home_in_round_rect(x, y, 170, 365, 305, 417, 26);
    if (track) {
        colour = ready ? rgb565(48, 209, 88) : rgb565(58, 58, 60);
    }
    const int knob_x = ready ? 278 : 197;
    if ((x - knob_x) * (x - knob_x) + (y - 391) * (y - 391) <= 21 * 21) {
        colour = text;
    }
    return colour;
}

static uint16_t fluid_settings_pixel(int x, int y)
{
    const int center_x = LCD_WIDTH / 2;
    const int center_y = LCD_HEIGHT / 2;
    if (!home_in_circle(x, y, center_x, center_y, 230)) {
        return 0;
    }

    uint16_t colour = rgb565(0, 0, 0);
    const uint16_t primary = rgb565(10, 132, 255);
    const uint16_t text = rgb565(245, 245, 247);
    const uint16_t muted = rgb565(142, 142, 147);
    const uint16_t selected = rgb565(16, 78, 139);
    const uint16_t panel = rgb565(28, 28, 30);
    const uint16_t border = rgb565(58, 58, 60);

    if (y == 96 && x >= 70 && x <= LCD_WIDTH - 71) {
        colour = border;
    }

    const int button_top[] = {112, 182, 252};
    for (int index = 0; index < 3; ++index) {
        const int top = button_top[index];
        const bool active = (int)s_fluid_shape == index;
        if (home_in_round_rect(x, y, 70, top, LCD_WIDTH - 71, top + 59, 16)) {
            bool button_border = !home_in_round_rect(x, y, 73, top + 3,
                                                      LCD_WIDTH - 74, top + 56, 13);
            colour = button_border ? (active ? primary : panel) :
                     (active ? selected : panel);
        }

        /* Three self-explanatory, text-free mode glyphs: liquid / blocks /
         * matrix.  The wide cards remain easy to tap on the round display. */
        const int glyph_x = center_x;
        const int glyph_y = top + 29;
        const int glyph_dx = x - glyph_x;
        const int glyph_dy = y - glyph_y;
        const uint16_t glyph_colour = active ? text : muted;
        if (index == FLUID_SHAPE_SIMPLE) {
            const int droplet_distance2 = glyph_dx * glyph_dx + (glyph_dy + 2) * (glyph_dy + 2);
            const bool droplet_tip = glyph_dy >= -15 && glyph_dy <= -5 &&
                                     (glyph_dx < 0 ? -glyph_dx : glyph_dx) <= glyph_dy + 15;
            if (droplet_distance2 <= 12 * 12 || droplet_tip) {
                colour = glyph_colour;
            }
        } else if (index == FLUID_SHAPE_BLOCKS) {
            const bool block = ((glyph_dx >= -17 && glyph_dx <= -5 && glyph_dy >= -10 && glyph_dy <= 2) ||
                                (glyph_dx >= 4 && glyph_dx <= 16 && glyph_dy >= -10 && glyph_dy <= 2) ||
                                (glyph_dx >= -6 && glyph_dx <= 6 && glyph_dy >= 7 && glyph_dy <= 19));
            if (block) {
                colour = glyph_colour;
            }
        } else {
            const int grid_x = glyph_dx + 14;
            const int grid_y = glyph_dy + 14;
            if (grid_x >= 0 && grid_x < 30 && grid_y >= 0 && grid_y < 30 &&
                (grid_x % 10) < 6 && (grid_y % 10) < 6) {
                colour = glyph_colour;
            }
        }
    }

    const int colour_top = 342;
    if (home_in_round_rect(x, y, 70, colour_top, LCD_WIDTH - 71, 424, 20)) {
        const bool button_border = !home_in_round_rect(x, y, 73, colour_top + 3,
                                                        LCD_WIDTH - 74, 421, 17);
        colour = button_border ? panel : panel;
        const int preview_dx = x - center_x;
        const int preview_dy = y - 383;
        const int preview_distance2 = preview_dx * preview_dx + preview_dy * preview_dy;
        if (preview_distance2 <= 28 * 28) {
            const liquid_palette_t *palette = active_palette();
            colour = rgb565(palette->body[0], palette->body[1], palette->body[2]);
        } else if (preview_distance2 <= 32 * 32) {
            colour = text;
        }
        /* Navigation chevron without an accompanying word label. */
        if (x >= 350 && x <= 374 && y >= 371 && y <= 395 &&
            ((x >= 366 && ((y - 383) < 3 && (y - 383) > -3)) ||
             (x >= 354 && x <= 366 && ((y - 383) == (x - 354) / 2 ||
                                        (y - 383) == -(x - 354) / 2)))) {
            colour = text;
        }
    }

    /* Circular icon-only navigation is the watchOS convention.  The larger
     * invisible y<92 hit area remains unchanged for reliable touch. */
    if (home_in_circle(x, y, 86, 73, 24)) {
        colour = rgb565(28, 28, 30);
    }
    const bool back_arrow = (x >= 78 && x <= 102 && y >= 65 && y <= 81 &&
                             ((x <= 84 && ((y - 73) < 3 && (y - 73) > -3)) ||
                              (x >= 84 && x <= 100 && y >= 71 && y <= 75)));
    if (back_arrow) {
        colour = text;
    }
    uint8_t alpha = 0;
    alpha = home_font_alpha(x, y, 187, 61, &lv_font_montserrat_28, "FLUID");
    if (alpha != 0) {
        return rgb565_blend(colour, text, alpha);
    }
    alpha = home_font_alpha(x, y, 74, 91, &lv_font_montserrat_18, "MODE");
    if (alpha != 0) {
        return rgb565_blend(colour, muted, alpha);
    }
    alpha = home_font_alpha(x, y, 70, 330, &lv_font_montserrat_18, "COLOR");
    if (alpha != 0) {
        return rgb565_blend(colour, muted, alpha);
    }
    return colour;
}

/* Static 301x301 wheel costs ~177 KiB in PSRAM, not another full canvas.
 * Allocation failure falls back to the exact same pixel formula. */
static uint16_t fluid_colour_wheel_pixel(int dx, int dy)
{
    const float two_pi = 6.283185307f;
    float hue = atan2f((float)dy, (float)dx) / two_pi;
    if (hue < 0.0f) hue += 1.0f;
    uint8_t red = 0, green = 0, blue = 0;
    hsv_to_rgb(hue, sqrtf((float)(dx * dx + dy * dy)) / 150.0f,
               0.96f, &red, &green, &blue);
    return rgb565(red, green, blue);
}

static void fluid_colour_prepare_cache(void)
{
    if (s_colour_wheel_cache_attempted) return;
    s_colour_wheel_cache_attempted = true;
    s_colour_wheel_cache = heap_caps_malloc(301 * 301 * sizeof(uint16_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_colour_wheel_cache == NULL) {
        ESP_LOGW(TAG, "Colour wheel cache unavailable; using pixel fallback");
        return;
    }
    for (int dy = -150; dy <= 150; ++dy) {
        for (int dx = -150; dx <= 150; ++dx) {
            s_colour_wheel_cache[(dy + 150) * 301 + dx + 150] =
                dx * dx + dy * dy <= 150 * 150 ? fluid_colour_wheel_pixel(dx, dy) : 0;
        }
    }
}

static uint16_t fluid_colour_picker_pixel(int x, int y)
{
    const int center_x = LCD_WIDTH / 2;
    const int center_y = LCD_HEIGHT / 2;
    if (!home_in_circle(x, y, center_x, center_y, 230)) {
        return 0;
    }

    uint16_t colour = rgb565(0, 0, 0);
    const uint16_t text = rgb565(245, 245, 247);
    const int wheel_x = center_x;
    const int wheel_y = 270;
    const int wheel_radius = 150;
    const int dx = x - wheel_x;
    const int dy = y - wheel_y;
    const int distance2 = dx * dx + dy * dy;

    if (y == 106 && x >= 70 && x <= LCD_WIDTH - 71) {
        colour = rgb565(58, 58, 60);
    }

    if (distance2 <= wheel_radius * wheel_radius) {
        colour = s_colour_wheel_cache != NULL ?
            s_colour_wheel_cache[(dy + 150) * 301 + dx + 150] :
            fluid_colour_wheel_pixel(dx, dy);
    }

    if (home_in_circle(x, y, 86, 73, 24)) {
        colour = rgb565(28, 28, 30);
    }
    /* A compact icon-only chevron preserves most of the wheel area. */
    const bool back_arrow = (x >= 75 && x <= 101 && y >= 65 && y <= 81 &&
                             ((x <= 82 && ((y - 73) < 3 && (y - 73) > -3)) ||
                              (x >= 82 && x <= 99 && y >= 71 && y <= 75)));
    if (back_arrow) {
        colour = text;
    }
    uint8_t alpha = home_font_alpha(x, y, 195, 61, &lv_font_montserrat_28, "COLOR");
    if (alpha != 0) {
        return rgb565_blend(colour, text, alpha);
    }
    return colour;
}

/* 0u0 is drawn from primitives instead of copied artwork.  The original app
 * is deliberately sparse: a white kawaii face floating on black, not a card
 * or an application chrome.  Keeping that rule here also guarantees every
 * pixel is repainted black on every 0u0 frame, so no previous UI can remain
 * as a line in the top-left corner. */
static bool ouo_in_ellipse(int x, int y, int center_x, int center_y,
                           int radius_x, int radius_y)
{
    const int dx = x - center_x;
    const int dy = y - center_y;
    return dx * dx * radius_y * radius_y + dy * dy * radius_x * radius_x <=
           radius_x * radius_x * radius_y * radius_y;
}

/* OuO's sad face uses solid eyes with the upper lids sloping inward.  The
 * small diagonal is more characteristic than a pair of thin closed arcs. */
static bool ouo_sad_eye_pixel(int dx, int dy, bool left)
{
    if (!ouo_in_ellipse(dx, dy, 0, 0, OUO_EYE_RADIUS, OUO_EYE_RADIUS)) {
        return false;
    }
    const int lid_y = -14 + (left ? -dx : dx) / 2;
    return dy >= lid_y;
}

static bool ouo_kiss_eye_pixel(int dx, int dy)
{
    /* The captured upper-lid family is narrower than an open eye: about
     * x=+-25 and y=-13..+7 in normalized 466px coordinates. */
    if (dx < -25 || dx > 25) {
        return false;
    }
    const int curve = -13 + (dx * dx) / 31;
    return dy >= curve - 3 && dy <= curve + 3;
}

static bool ouo_pressed_eye_pixel(int dx, int dy, bool left)
{
    if (abs(dx) > 28) return false;
    const int u = left ? dx : -dx;
    const int curve = -7 + u * 34 / 100 + dx * dx * 9 / 1000;
    const int width = 1 + 3 * (784 - dx * dx) / 784;
    return abs(dy - curve) <= width;
}

/* The surprised mouth is a solid rounded shape with a nearly flat top, not a
 * symmetric ellipse. */
static bool ouo_flat_mouth_pixel(int dx, int dy, int half_width, int height)
{
    if (dx < -half_width || dx > half_width || dy < -height || dy > height) {
        return false;
    }
    const int top = -(height * 3) / 5;
    if (dy < top) {
        return false;
    }
    const int lower = dy - top;
    const int max_half = half_width - (lower * lower) / (height * height / 2 + 1);
    return abs(dx) <= max_half;
}

/* OuO's most recognisable "closed" eye is not a thin line.  It is the lower
 * half of a white circle with a perfectly flat lid, as seen when an eye is
 * poked or during a blink.  The Android app also uses the corresponding
 * upper dome for the pressed-mouth animation. */
static bool ouo_lower_dome_pixel(int dx, int dy, int radius_x, int radius_y)
{
    return dy >= -8 &&
           dx * dx * radius_y * radius_y + dy * dy * radius_x * radius_x <=
           radius_x * radius_x * radius_y * radius_y;
}

static bool ouo_line_pixel(int x, int y, int x0, int y0, int x1, int y1,
                           int half_width);

/* The shake face uses two identical, same-direction Archimedean spirals.
 * Do not mirror one eye: that looks like paired brackets rather than the
 * familiar two coiled "mosquito" eyes from OuO.  Two radial solutions for
 * each angle make the curve complete nearly two turns inside a 33 px eye. */
static bool ouo_dizzy_eye_pixel(int dx, int dy, float phase)
{
    /* Reject the vast majority of face pixels with integer math before
     * entering the expensive polar conversion.  Without this clip every
     * 475x258 OuO frame performed sqrtf/atan2f for the black background too,
     * stretching the real frame interval well beyond the 16 ms target. */
    const int distance2 = dx * dx + dy * dy;
    if (distance2 > 1089) { /* 33 px eye radius squared */
        return false;
    }
    const float radius = sqrtf((float)(dx * dx + dy * dy));
    if (radius > 33.0f) {
        return false;
    }
    float angle = atan2f((float)dy, (float)dx);
    if (angle < 0.0f) {
        angle += 6.2831853f;
    }
    angle += phase;
    while (angle >= 6.2831853f) {
        angle -= 6.2831853f;
    }
    while (angle < 0.0f) {
        angle += 6.2831853f;
    }
    const float inner_turn = 3.5f + 2.35f * angle;
    const float outer_turn = 3.5f + 2.35f * (angle + 6.2831853f);
    return radius <= 3.7f || fabsf(radius - inner_turn) <= 2.25f ||
           fabsf(radius - outer_turn) <= 2.25f;
}

/* A solid white smile/frown stroke.  A parabola keeps the contour smooth at
 * this panel's resolution while allowing the cheerful U mouth to be visibly
 * thicker than a font-like line. */
static bool ouo_curve_mouth_pixel(int dx, int dy, int half_width, int sag,
                                  int half_thickness, bool smile)
{
    if (dx < -half_width || dx > half_width) {
        return false;
    }
    const int curve = smile ?
                      sag - (dx * dx * sag) / (half_width * half_width) :
                      -sag + (dx * dx * sag) / (half_width * half_width);
    return dy >= curve - half_thickness && dy <= curve + half_thickness;
}

/* OuO's resting mouth is not a sharp typographic "w".  It is a very small,
 * rounded double dip: the centre and both ends sit slightly high, with two
 * shallow, soft valleys.  Keeping it as one primitive lets the different
 * moods alter the same familiar mouth instead of replacing it with symbols. */
static bool ouo_w_mouth_pixel(int dx, int dy, int half_width, int depth,
                               int half_thickness)
{
    if (dx < -half_width || dx > half_width) {
        return false;
    }
    const int distance = dx < 0 ? -dx : dx;
    const int valley = half_width / 2;
    const int offset = valley > 0 ?
                       depth * (valley - (distance > valley ?
                                          distance - valley : valley - distance)) / valley : 0;
    return dy >= offset - half_thickness && dy <= offset + half_thickness;
}

static bool ouo_line_pixel(int x, int y, int x0, int y0, int x1, int y1,
                           int half_width)
{
    const int vx = x1 - x0;
    const int vy = y1 - y0;
    const int wx = x - x0;
    const int wy = y - y0;
    const int length2 = vx * vx + vy * vy;
    const int projection = wx * vx + wy * vy;
    if (projection < 0 || projection > length2) {
        return false;
    }
    const int64_t cross = (int64_t)vx * wy - (int64_t)vy * wx;
    return cross * cross <= (int64_t)half_width * half_width * length2;
}

/* OuO's kiss is a thick side-opening "3" stroke.  The two lobes peak near
 * the centre-left and meet a short vertical on the right, matching the
 * captured mouth rather than a typographic brace. */
static bool ouo_kiss_mouth_pixel(int dx, int dy)
{
    return ouo_line_pixel(dx, dy, -9, -16, -6, -23, 4) ||
           ouo_line_pixel(dx, dy, -6, -23, 0, -25, 4) ||
           ouo_line_pixel(dx, dy, 0, -25, 10, -25, 4) ||
           ouo_line_pixel(dx, dy, 10, -25, 16, -16, 4) ||
           ouo_line_pixel(dx, dy, 16, -16, 16, -7, 4) ||
           ouo_line_pixel(dx, dy, 16, -7, 3, 0, 4) ||
           ouo_line_pixel(dx, dy, 3, 0, 16, 7, 4) ||
           ouo_line_pixel(dx, dy, 16, 7, 16, 16, 4) ||
           ouo_line_pixel(dx, dy, 16, 16, 10, 25, 4) ||
           ouo_line_pixel(dx, dy, 10, 25, 0, 25, 4) ||
           ouo_line_pixel(dx, dy, 0, 25, -6, 23, 4) ||
           ouo_line_pixel(dx, dy, -6, 23, -9, 16, 4);
}

/* The pull recording shows a filled left-facing "3" plus a separate right
 * arc.  The centre bridge is intentionally solid white, while the small gap
 * before the arc remains black.  Mirroring keeps left and right drags
 * symmetrical. */
static bool ouo_pull_side_mouth_pixel(int dx, int dy, bool right, int stretch)
{
    if (!right) {
        dx = -dx;
    }
    const int s = clamp_int(stretch, 0, 8);
    const int core_h = 13 + s;
    const int core_left = -7 - s / 2;
    const int core_right = 3 + s / 4;
    const int arc_x = 14 + s;
    const int arc_h = 21 + s;
    const int arc_inner = 7 + s / 2;
    /* Keep the captured 3+arc contour stable while the whole mouth follows
     * the finger.  Scaling this path by travel distance made it appear to
     * grow merely because the pointer was farther from the mouth centre. */
    if (ouo_line_pixel(dx, dy, core_right, -core_h * 9 / 10,
                       core_left, -core_h - 2, 4) ||
        ouo_line_pixel(dx, dy, core_left, -core_h - 2,
                       core_left, -core_h * 42 / 100, 4) ||
        ouo_line_pixel(dx, dy, core_left, -core_h * 42 / 100,
                       core_right, 0, 4) ||
        ouo_line_pixel(dx, dy, core_right, 0,
                       core_left, core_h * 42 / 100, 4) ||
        ouo_line_pixel(dx, dy, core_left, core_h * 42 / 100,
                       core_left, core_h + 2, 4) ||
        ouo_line_pixel(dx, dy, core_left, core_h + 2,
                       core_right, core_h * 9 / 10, 4) ||
        (dx >= core_left + 2 && dx <= 8 + s / 2 &&
         dy >= -3 - s / 2 && dy <= 3 + s / 2)) {
        return true;
    }
    /* Keep the independent arc paired with the fixed-size 3 and its small
     * black gap. */
    return ouo_line_pixel(dx, dy, arc_x, -arc_h, arc_x - arc_inner, 0, 4) ||
           ouo_line_pixel(dx, dy, arc_x - arc_inner, 0, arc_x, arc_h, 4);
}

static bool ouo_rounded_rect_pixel(int x, int y, int half_width,
                                   int half_height, int radius)
{
    if (abs(x) > half_width || abs(y) > half_height) {
        return false;
    }
    const int corner_x = abs(x) - (half_width - radius);
    const int corner_y = abs(y) - (half_height - radius);
    if (corner_x > 0 && corner_y > 0) {
        return corner_x * corner_x + corner_y * corner_y <= radius * radius;
    }
    return true;
}

/* Convert an axis-aligned box into the reference mouth contour.  The top is
 * flat and the lower edge is a shallow U, matching the Android stretch frame
 * rather than a rotated rectangle. */
static bool ouo_flat_mouth_box_pixel(int dx, int dy, int left, int right,
                                     int top, int bottom)
{
    const int half_width = (right - left) / 2;
    const int box_height = bottom - top;
    if (half_width <= 0 || box_height <= 0) {
        return false;
    }
    /* ouo_flat_mouth_pixel's active contour is 1.6 * height tall.  Choose its
     * local origin so the active top/bottom land on the requested box. */
    const int local_height = clamp_int(box_height * 5 / 8, 1, 90);
    const int center_x = (left + right) / 2;
    const int center_y = top + local_height * 3 / 5;
    return ouo_flat_mouth_pixel(dx - center_x, dy - center_y,
                                half_width, local_height);
}

static int ouo_pull_t(int magnitude)
{
    if (magnitude <= 4) {
        return 0;
    }
    return clamp_int((magnitude - 4) * 256 / 58, 0, 256);
}

/* The stretch family is continuous: a vertical pull grows a narrow capsule,
 * while a horizontal pull grows the broad reference blob.  For diagonal
 * vectors the two axes interpolate without rotating the mouth. */
static bool ouo_stretch_mouth_pixel(int dx, int dy, int travel_x, int travel_y)
{
    const int ax = abs(travel_x);
    const int ay = abs(travel_y);
    const int magnitude = ax > ay ? ax : ay;
    const int t = ouo_pull_t(magnitude);
    const bool horizontal = ax * 4 >= ay * 5;
    const bool vertical = ay * 4 >= ax * 5;
    const int h = vertical ? 0 : (horizontal ? 256 :
                                  (magnitude > 0 ? ax * 256 / magnitude : 0));
    const int half_width = 21 + (42 * h * t) / (256 * 256);
    const int half_height = clamp_int(
        20 + t * (15 + 27 * (256 - h) / 256) / 256, 20, 42);
    int left = -half_width;
    int right = half_width;
    int top = -half_height;
    int bottom = half_height;

    /* Anchor the edge opposite to the finger.  This prevents a right pull
     * from mysteriously expanding to the left (and the same on Y). */
    if (horizontal) {
        if (travel_x > 0) {
            left = -21;
            right = left + half_width * 2;
        } else if (travel_x < 0) {
            right = 21;
            left = right - half_width * 2;
        }
    } else if (vertical) {
        if (travel_y > 0) {
            top = -20;
            bottom = top + half_height * 2;
        } else if (travel_y < 0) {
            bottom = 20;
            top = bottom - half_height * 2;
        }
    }
    return ouo_flat_mouth_box_pixel(dx, dy, left, right, top, bottom);
}

/* The square family is deliberately screen-aligned.  Its four edges remain
 * horizontal/vertical while a small, direction-dependent bulge makes it an
 * irregular shape instead of the old fixed diamond or perfect square. */
static bool ouo_square_mouth_pixel(int dx, int dy, int travel_x, int travel_y)
{
    const int ax = abs(travel_x);
    const int ay = abs(travel_y);
    const int magnitude = ax > ay ? ax : ay;
    const int t = ouo_pull_t(magnitude);
    const int h = magnitude > 0 ? ax * 256 / magnitude : 0;
    const int v = magnitude > 0 ? ay * 256 / magnitude : 0;
    const int half_width = 24 + t * (18 + 12 * h / 256) / 256;
    const int half_height = 22 + t * (18 + 12 * v / 256) / 256;
    const int lean_x = (t * (4 + 6 * h / 256)) / 256;
    const int lean_y = (t * (4 + 6 * v / 256)) / 256;
    const int left = -(half_width + (travel_x < 0 ? lean_x : 0));
    const int right = half_width + (travel_x > 0 ? lean_x : 0);
    const int top = -(half_height + (travel_y < 0 ? lean_y : 0));
    const int bottom = half_height + (travel_y > 0 ? lean_y : 0);
    const int center_x = (left + right) / 2;
    const int center_y = (top + bottom) / 2;
    const int radius = clamp_int(7 + t * 5 / 256, 7, 12);
    return ouo_rounded_rect_pixel(dx - center_x, dy - center_y,
                                  (right - left) / 2,
                                  (bottom - top) / 2, radius);
}

static bool ouo_round_mouth_pixel(int dx, int dy, int travel_x, int travel_y)
{
    const int ax = abs(travel_x);
    const int ay = abs(travel_y);
    const int magnitude = ax > ay ? ax : ay;
    const int t = ouo_pull_t(magnitude);
    /* Match the preview's larger round family.  The old 41x38px cap made a
     * long Android-style drag look almost unchanged on the device. */
    const int radius_x = 21 + t * 32 / 256;
    const int radius_y = 20 + t * 26 / 256;
    return ouo_in_ellipse(dx, dy, 0, 0, radius_x, radius_y);
}

static bool ouo_triangle_mouth_pixel(int dx, int dy, int travel_x, int travel_y)
{
    /* Touch-visible recording 208.5–211s: independent width/height, upright
     * throughout. Same quadratic flanks as liveTrianglePullMouth in JS. */
    const int ax = clamp_int(abs(travel_x), 0, 84);
    const int ay = clamp_int(abs(travel_y), 0, 84);
    const int tip = 18 + ay * 12 / 84;
    const int base = 14 + ay * 42 / 84;
    const int half = 18 + ax * 48 / 84;
    if (dy < -tip || dy > base) {
        return false;
    }
    const int u = (dy + tip) * 256 / (tip + base);
    const int span = half * (8 * u * 256 - 3 * u * u) / (5 * 256 * 256);
    return abs(dx) <= span;
}

/* Dispatch the variant selected on the first significant sample.  Once
 * selected, subsequent jitter changes only the size/direction within this
 * family; it cannot turn a side pull into a square or vice versa. */
static bool ouo_drag_mouth_pixel(int dx, int dy, int travel_x, int travel_y,
                                 ouo_mouth_variant_t variant)
{
    const int ax = abs(travel_x);
    const int ay = abs(travel_y);
    if (ax < 10 && ay < 10 && variant != OUO_MOUTH_VARIANT_TRIANGLE) {
        return ouo_flat_mouth_pixel(dx, dy, 31, 29);
    }
    switch (variant) {
    case OUO_MOUTH_VARIANT_SIDE:
        return ouo_pull_side_mouth_pixel(dx, dy, travel_x >= 0,
                                         s_ouo_mouth_stretch_pixels);
    case OUO_MOUTH_VARIANT_ROUND:
        return ouo_round_mouth_pixel(dx, dy, travel_x, travel_y);
    case OUO_MOUTH_VARIANT_SQUARE:
        return ouo_square_mouth_pixel(dx, dy, travel_x, travel_y);
    case OUO_MOUTH_VARIANT_TRIANGLE:
        return ouo_triangle_mouth_pixel(dx, dy, travel_x, travel_y);
    case OUO_MOUTH_VARIANT_STRETCH:
    default:
        return ouo_stretch_mouth_pixel(dx, dy, travel_x, travel_y);
    }
}

static bool ouo_eye_hit(int x, int y, int center_x, int center_y)
{
    return ouo_in_ellipse(x, y, center_x, center_y, 66, 82);
}

/* Passive playback is separate from interactive expressions. Decode only when
 * a reference event changes; long recorded holds do not redraw every tick. */
static void ouo_idle_step(int64_t now)
{
    s_ouo_decode_last_us = 0;
    if (!s_ouo_auto_expressions || s_ouo_touch_active ||
        s_ouo_expression != OUO_EXPRESSION_IDLE ||
        s_ouo_mouth_x_offset || s_ouo_mouth_y_offset || s_ouo_squish_pixels) {
        if (s_ouo_idle_visible) s_ouo_dirty = true;
        s_ouo_idle_visible = false;
        s_ouo_idle_epoch_us = 0;
        return;
    }
    if (s_ouo_idle_tiles == NULL) return;
    if (s_ouo_idle_epoch_us == 0) s_ouo_idle_epoch_us = now;
    const uint32_t ms = (uint32_t)(((now - s_ouo_idle_epoch_us) / 1000LL) % OUO_IDLE_DURATION_MS);
    uint32_t low = 0, high = OUO_IDLE_EVENT_COUNT;
    while (low + 1 < high) {
        const uint32_t mid = (low + high) / 2;
        if (ouo_idle_events[mid][0] <= ms) low = mid;
        else high = mid;
    }
    const uint32_t frame = ouo_idle_events[low][1];
    if (frame != s_ouo_idle_frame) {
        const int64_t decode_start_us = esp_timer_get_time();
        uint32_t pos = 0;
        for (uint32_t i = ouo_idle_offsets[frame]; i < ouo_idle_offsets[frame + 1]; i += 2) {
            const uint32_t count = ouo_idle_rle[i];
            if (pos + count > OUO_IDLE_TILE_PIXELS) return; // Generated-data guard.
            memset(s_ouo_idle_tiles + pos, ouo_idle_rle[i + 1] * 17, count);
            pos += count;
        }
        if (pos != OUO_IDLE_TILE_PIXELS) return;
        s_ouo_idle_frame = frame;
        s_ouo_dirty = true;
        s_ouo_decode_last_us = esp_timer_get_time() - decode_start_us;
    }
    if (!s_ouo_idle_visible) s_ouo_dirty = true;
    s_ouo_idle_visible = true;
}

static uint16_t ouo_idle_pixel(int x, int y)
{
    uint8_t gray = 0;
    int tx = x - (OUO_LEFT_EYE_X - 48), ty = y - (OUO_EYE_Y - 42);
    if (tx >= 0 && tx < 96 && ty >= 0 && ty < 84) gray = s_ouo_idle_tiles[ty * 96 + tx];
    tx = x - (OUO_RIGHT_EYE_X - 48);
    if (tx >= 0 && tx < 96 && ty >= 0 && ty < 84) {
        const uint8_t value = s_ouo_idle_tiles[96 * 84 + ty * 96 + tx];
        if (value > gray) gray = value;
    }
    tx = x - (OUO_FACE_CENTER_X - 48); ty = y - (OUO_MOUTH_Y - 50);
    if (tx >= 0 && tx < 96 && ty >= 0 && ty < 100) {
        const uint8_t value = s_ouo_idle_tiles[96 * 168 + ty * 96 + tx];
        if (value > gray) gray = value;
    }
    return rgb565(gray, gray, gray);
}

/* Already-decoded idle tiles do not need the general expression evaluator.
 * Preserve its exact max-gray compositing, coordinates and clipping, but
 * visit only the 25,728 tile pixels (not 122,550 mostly-black face pixels).
 * The palette is internal RAM; the original 25KB PSRAM decode buffer stays
 * unchanged, and no allocation happens on the per-frame path. */
static void ouo_blit_idle_tiles(const dirty_rect_t *dirty)
{
    if (!s_ouo_idle_palette_ready) {
        for (int gray = 0; gray < 256; ++gray) {
            s_ouo_idle_palette[gray] = rgb565(gray, gray, gray);
        }
        s_ouo_idle_palette_ready = true;
    }
    const int shift_x = s_ouo_gaze_x + s_ouo_shake_x;
    const int shift_y = s_ouo_gaze_y + s_ouo_shake_y;
    const int lefts[3] = {OUO_LEFT_EYE_X - 48 + shift_x,
                          OUO_RIGHT_EYE_X - 48 + shift_x,
                          OUO_FACE_CENTER_X - 48 + shift_x};
    const int tops[3] = {OUO_EYE_Y - 42 + shift_y, OUO_EYE_Y - 42 + shift_y,
                         OUO_MOUTH_Y - 50 + shift_y};
    const int heights[3] = {84, 84, 100};
    const int offsets[3] = {0, 96 * 84, 96 * 168};
    for (int tile = 0; tile < 3; ++tile) {
        const int left = lefts[tile], top = tops[tile];
        const int x0 = left < dirty->x1 ? dirty->x1 - left : 0;
        const int x1 = left + 96 > dirty->x2 ? dirty->x2 - left : 96;
        const int y0 = top < dirty->y1 ? dirty->y1 : top;
        const int y1 = top + heights[tile] > dirty->y2 ? dirty->y2 : top + heights[tile];
        if (x0 >= x1 || y0 >= y1) continue;
        for (int y = y0; y < y1; ++y) {
            const uint8_t *source = s_ouo_idle_tiles + offsets[tile] + (y - top) * 96 + x0;
            uint16_t *target = s_lcd_canvas + (size_t)y * LCD_WIDTH + left + x0;
            for (int x = 0; x < x1 - x0; ++x) {
                const uint16_t colour = s_ouo_idle_palette[source[x]];
                if (colour > target[x]) target[x] = colour;
            }
        }
    }
}

static uint16_t ouo_pixel(int x, int y)
{
    const int center_x = OUO_FACE_CENTER_X;
    const uint16_t ink = rgb565(0, 0, 0);
    const uint16_t white = rgb565(250, 250, 250);
    /* Geometry is scaled from captures of OuO 1.611 rather than reconstructed
     * from the launcher icon. */
    const int face_shift_x = s_ouo_gaze_x + s_ouo_shake_x;
    const int face_shift_y = s_ouo_gaze_y + s_ouo_shake_y;
    if (s_ouo_idle_visible && s_ouo_idle_tiles != NULL &&
        s_ouo_auto_expressions && !s_ouo_touch_active &&
        s_ouo_expression == OUO_EXPRESSION_IDLE) {
        return ouo_idle_pixel(x - face_shift_x, y - face_shift_y);
    }
    const int squish = s_ouo_squish_pixels;
    const int eye_squeeze = squish / 2;
    const int left_center_x = OUO_LEFT_EYE_X + face_shift_x + eye_squeeze;
    const int right_center_x = OUO_RIGHT_EYE_X + face_shift_x - eye_squeeze;
    const int eye_center_y = OUO_EYE_Y + face_shift_y;
    const int left_dx = x - left_center_x;
    const int right_dx = x - right_center_x;
    const int eye_dy = y - eye_center_y;
    const bool triangle_drag = s_ouo_mouth_touch_active && s_ouo_mouth_variant_selected &&
                               s_ouo_mouth_variant == OUO_MOUTH_VARIANT_TRIANGLE;
    const bool tilt_left = s_ouo_tilt_reactions && s_ouo_expression == OUO_EXPRESSION_IDLE &&
                           s_ouo_gaze_x < -46;
    const bool tilt_right = s_ouo_tilt_reactions && s_ouo_expression == OUO_EXPRESSION_IDLE &&
                            s_ouo_gaze_x > 46;
    const bool lower_left = s_ouo_expression == OUO_EXPRESSION_BLINK ||
                             s_ouo_expression == OUO_EXPRESSION_SQUISH || tilt_left;
    const bool lower_right = s_ouo_expression == OUO_EXPRESSION_BLINK ||
                               s_ouo_expression == OUO_EXPRESSION_SQUISH || tilt_right;
    /* The original face's open eyes are plain solid white circles.  Do not
     * add pupils: its charm is the very restrained black-and-white geometry. */
    const bool solid_left =
                            s_ouo_expression == OUO_EXPRESSION_IDLE ||
                            s_ouo_expression == OUO_EXPRESSION_CARET ||
                            s_ouo_expression == OUO_EXPRESSION_WINK_RIGHT ||
                            s_ouo_expression == OUO_EXPRESSION_SURPRISED || tilt_right;
    const bool solid_right =
                             s_ouo_expression == OUO_EXPRESSION_IDLE ||
                             s_ouo_expression == OUO_EXPRESSION_CARET ||
                             s_ouo_expression == OUO_EXPRESSION_WINK_LEFT ||
                             s_ouo_expression == OUO_EXPRESSION_SURPRISED || tilt_left;
    if ((lower_left && ouo_lower_dome_pixel(left_dx, eye_dy,
                                             OUO_EYE_RADIUS, OUO_EYE_RADIUS)) ||
        (lower_right && ouo_lower_dome_pixel(right_dx, eye_dy,
                                              OUO_EYE_RADIUS, OUO_EYE_RADIUS))) {
        return white;
    }

    if ((!triangle_drag || eye_dy <= OUO_EYE_RADIUS * 4 / 5) &&
        ((solid_left && ouo_in_ellipse(x, y, left_center_x, eye_center_y,
                                      OUO_EYE_RADIUS, OUO_EYE_RADIUS)) ||
        (solid_right && ouo_in_ellipse(x, y, right_center_x, eye_center_y,
                                       OUO_EYE_RADIUS, OUO_EYE_RADIUS)))) {
        return white;
    }

    if ((s_ouo_expression == OUO_EXPRESSION_WINK_LEFT &&
         ouo_pressed_eye_pixel(left_dx, eye_dy, true)) ||
        (s_ouo_expression == OUO_EXPRESSION_WINK_RIGHT &&
         ouo_pressed_eye_pixel(right_dx, eye_dy, false))) return white;

    if (s_ouo_expression == OUO_EXPRESSION_MOUTH_RELEASE) {
        if (ouo_pressed_eye_pixel(left_dx, eye_dy, true) ||
            ouo_pressed_eye_pixel(right_dx, eye_dy, false)) return white;
    } else if (s_ouo_expression == OUO_EXPRESSION_HEAD_PAT) {
        if (abs(eye_dy) <= 4 && (abs(left_dx) <= OUO_EYE_RADIUS ||
                                 abs(right_dx) <= OUO_EYE_RADIUS)) return white;
    } else if (s_ouo_expression == OUO_EXPRESSION_DELIGHTED) {
        if (eye_dy <= OUO_EYE_RADIUS * 2 / 5 &&
            (ouo_in_ellipse(x, y, left_center_x, eye_center_y, OUO_EYE_RADIUS, OUO_EYE_RADIUS) ||
             ouo_in_ellipse(x, y, right_center_x, eye_center_y, OUO_EYE_RADIUS, OUO_EYE_RADIUS))) return white;
        if (ouo_in_ellipse(left_dx, eye_dy, -28, 29, 13, 5) ||
            ouo_in_ellipse(right_dx, eye_dy, 28, 29, 13, 5)) return rgb565(69, 69, 69);
    } else if (s_ouo_expression == OUO_EXPRESSION_DIZZY) {
        if (ouo_dizzy_eye_pixel(left_dx, eye_dy, s_ouo_dizzy_phase) ||
            ouo_dizzy_eye_pixel(right_dx, eye_dy, s_ouo_dizzy_phase)) {
            return white;
        }
    } else if (s_ouo_expression == OUO_EXPRESSION_ANGRY) {
        /* The original's irritated face is two clear inward chevrons, `><`,
         * not merely circular eyes with a slanted crop. */
        if (ouo_line_pixel(left_dx, eye_dy, -27, -21, 10, 0, 5) ||
            ouo_line_pixel(left_dx, eye_dy, -27, 21, 10, 0, 5) ||
            ouo_line_pixel(right_dx, eye_dy, 27, -21, -10, 0, 5) ||
            ouo_line_pixel(right_dx, eye_dy, 27, 21, -10, 0, 5)) {
            return white;
        }
    } else if (s_ouo_expression == OUO_EXPRESSION_KISS ||
               s_ouo_expression == OUO_EXPRESSION_HAPPY) {
        if (ouo_kiss_eye_pixel(left_dx, eye_dy) ||
            ouo_kiss_eye_pixel(right_dx, eye_dy)) {
            return white;
        }
    } else if (s_ouo_expression == OUO_EXPRESSION_SLEEPY) {
        if (ouo_lower_dome_pixel(left_dx, eye_dy - 12, 30, 19) ||
            ouo_lower_dome_pixel(right_dx, eye_dy - 12, 30, 19)) {
            return white;
        }
    } else if (s_ouo_expression == OUO_EXPRESSION_SAD) {
        if (ouo_sad_eye_pixel(left_dx, eye_dy, true) ||
            ouo_sad_eye_pixel(right_dx, eye_dy, false)) {
            return white;
        }
    }

    /* A grabbed mouth follows the fingertip.  This is intentionally separate
     * from its width: dragging left or right should pull the whole mouth,
     * rather than make the same oval increasingly wide. */
    const int triangle_x = clamp_int(s_ouo_last_touch_x - s_ouo_touch_origin_x, -120, 120);
    const int triangle_y = clamp_int(s_ouo_last_touch_y - s_ouo_touch_origin_y, -110, 110);
    /* For this family, use the real gesture vector for deformation and only
     * a small translation for placement, just like the preview. Clipped
     * placement offsets must never double as the deformation input. */
    const int mouth_dx = x - (center_x + face_shift_x +
        (triangle_drag ? triangle_x * 18 / 110 : s_ouo_mouth_x_offset));
    const int mouth_dy = y - (OUO_MOUTH_Y + face_shift_y + squish / 8 +
        (triangle_drag ? 6 + triangle_y * 14 / 110 : s_ouo_mouth_y_offset));
    const int resting_mouth_width = clamp_int(25 + s_ouo_mouth_stretch_pixels, 14, 52);
    const int resting_mouth_depth = clamp_int(8 - s_ouo_mouth_stretch_pixels / 6, 4, 11);
    bool mouth = false;
    if (s_ouo_expression == OUO_EXPRESSION_HEAD_PAT) {
        /* Two smooth valleys matching catMouth(), not the angular old W. */
        const int a = abs(mouth_dx);
        if (a <= 32) {
            const int u = (32 - a) * 256 / 32;
            const int curve = -6 + (52 * u * 256 - 48 * u * u) / (256 * 256);
            mouth = abs(mouth_dy - curve) <= 3;
        }
    } else if (s_ouo_expression == OUO_EXPRESSION_DELIGHTED) {
        mouth = mouth_dy >= -23 && ouo_in_ellipse(mouth_dx, mouth_dy, 0, 1, 32, 30);
    } else if (s_ouo_expression == OUO_EXPRESSION_MOUTH_RELEASE) {
        mouth = ouo_line_pixel(mouth_dx, mouth_dy, -18, 0, 18, 0, 3);
    } else if (s_ouo_expression == OUO_EXPRESSION_IDLE ||
        s_ouo_expression == OUO_EXPRESSION_BLINK ||
        s_ouo_expression == OUO_EXPRESSION_HAPPY) {
        /* Normal/cheerful OuO is a compact, deep U smile.  The earlier
         * 32x14 stroke was too wide and shallow on the round panel. */
        mouth = ouo_curve_mouth_pixel(mouth_dx, mouth_dy, 29, 17, 4, true);
    } else if (s_ouo_expression == OUO_EXPRESSION_ANGRY) {
        mouth = ouo_curve_mouth_pixel(mouth_dx, mouth_dy, 31, 10, 4, false);
    } else if (s_ouo_expression == OUO_EXPRESSION_SAD) {
        mouth = ouo_curve_mouth_pixel(mouth_dx, mouth_dy, 25, 8, 3, false);
    } else if (s_ouo_expression == OUO_EXPRESSION_SLEEPY) {
        mouth = ouo_curve_mouth_pixel(mouth_dx, mouth_dy, 20, 3, 3, false);
    } else if (s_ouo_expression == OUO_EXPRESSION_SURPRISED) {
        /* A live mouth pull follows the full contact vector.  This preserves
         * the filled side-pull at the far horizontal limit while adding the
         * vertical pill, diagonal square and upward triangle families. */
        if (triangle_drag) {
            mouth = ouo_triangle_mouth_pixel(mouth_dx, mouth_dy, triangle_x, triangle_y);
        } else if (s_ouo_mouth_touch_active &&
            (abs(s_ouo_mouth_x_offset) >= 10 ||
             abs(s_ouo_mouth_y_offset) >= 10)) {
            mouth = ouo_drag_mouth_pixel(mouth_dx, mouth_dy,
                                         s_ouo_mouth_x_offset,
                                         s_ouo_mouth_y_offset,
                                         s_ouo_mouth_variant);
        } else {
            mouth = ouo_flat_mouth_pixel(mouth_dx, mouth_dy, 31, 29);
        }
    } else if (s_ouo_expression == OUO_EXPRESSION_KISS) {
        mouth = ouo_kiss_mouth_pixel(mouth_dx, mouth_dy);
    } else if (s_ouo_expression == OUO_EXPRESSION_SQUISH) {
        /* Cheek squeezing is a compact, friendly response: flat-topped lower
         * dome eyes and a thick U smile.  The previous upper domes plus a
         * giant filled bowl read as a horror mask on the round panel. */
        mouth = ouo_curve_mouth_pixel(mouth_dx, mouth_dy, 34, 15, 5, true);
    } else if (s_ouo_expression == OUO_EXPRESSION_DIZZY) {
        mouth = ouo_line_pixel(mouth_dx, mouth_dy, -24, 10, 0, -12, 3) ||
                ouo_line_pixel(mouth_dx, mouth_dy, 0, -12, 24, 10, 3);
    } else if (s_ouo_expression == OUO_EXPRESSION_WINK_LEFT ||
               s_ouo_expression == OUO_EXPRESSION_WINK_RIGHT ||
               s_ouo_expression == OUO_EXPRESSION_CARET) {
        mouth = ouo_line_pixel(mouth_dx, mouth_dy, -27, 12, 0, -13, 3) ||
                ouo_line_pixel(mouth_dx, mouth_dy, 0, -13, 27, 12, 3);
    } else {
        mouth = ouo_w_mouth_pixel(mouth_dx, mouth_dy, resting_mouth_width + 4,
                                   resting_mouth_depth + 1, 3);
    }
    if (mouth) {
        return white;
    }
    return ink;
}

static uint16_t ouo_menu_pixel(int x, int y)
{
    const uint16_t ink = rgb565(0, 0, 0);
    const uint16_t white = rgb565(245, 245, 247);
    const uint16_t panel = rgb565(28, 28, 30);
    const uint16_t separator = rgb565(58, 58, 60);
    const uint16_t blue = rgb565(10, 132, 255);
    const uint16_t green = rgb565(48, 209, 88);
    const int slider_left = 86;
    const int slider_right = LCD_WIDTH - 86;
    const int marker_x = slider_left +
                         (slider_right - slider_left) * s_ouo_mood / 100;
    uint16_t colour = ink;

    /* The face remains pure black; its hidden controls use one watch-style
     * grouped card so settings feel native without changing OuO itself. */
    const int card_left = 54;
    const int card_right = LCD_WIDTH - 54;
    const int card_top = 94;
    const int card_bottom = 372;
    if (home_in_round_rect(x, y, card_left, card_top,
                           card_right, card_bottom, 28)) {
        colour = panel;
    }
    if ((y == 292 || y == 330) && x >= 104 && x <= 394) {
        colour = separator;
    }

    if (home_in_circle(x, y, 90, 166, 28)) {
        colour = rgb565(44, 44, 46);
    }
    /* Back icon stays inside its existing forgiving touch region. */
    if (ouo_line_pixel(x, y, 104, 142, 80, 166, 3) ||
        ouo_line_pixel(x, y, 80, 166, 104, 190, 3)) {
        colour = white;
    }

    if (x >= slider_left && x <= slider_right && y >= 245 && y <= 248) {
        colour = separator;
    }
    if (x >= marker_x - 9 && x <= marker_x + 9 && y >= 232 && y <= 261) {
        const int dx = x - marker_x;
        const int dy = y - 246;
        if (dx * dx + dy * dy <= 10 * 10) {
            colour = blue;
        }
    }
    if (x >= slider_left && x <= marker_x && y >= 245 && y <= 248) {
        colour = blue;
    }

    /* The whole row remains tappable; visible switches use the familiar
     * green enabled state and neutral disabled state. */
    const bool auto_row = y >= 304 && y <= 324;
    const bool tilt_row = y >= 342 && y <= 362;
    const bool auto_on = s_ouo_auto_expressions;
    const bool tilt_on = s_ouo_tilt_reactions;
    if ((auto_row || tilt_row) &&
        home_in_round_rect(x, y, 326, auto_row ? 302 : 340,
                           394, auto_row ? 326 : 364, 12)) {
        const bool enabled = auto_row ? auto_on : tilt_on;
        colour = enabled ? green : rgb565(72, 72, 74);
        const int knob_x = enabled ? 380 : 340;
        const int knob_y = auto_row ? 314 : 352;
        if (ouo_in_ellipse(x, y, knob_x, knob_y, 10, 10)) {
            colour = white;
        }
    }

    uint8_t alpha = home_font_alpha(x, y, 173, 136, &lv_font_montserrat_28, "MOOD");
    if (alpha > 0) {
        return rgb565_blend(colour, white, alpha);
    }
    alpha = home_font_alpha(x, y, 104, 300, &lv_font_montserrat_18, "AUTO");
    if (alpha > 0) {
        return rgb565_blend(colour, white, alpha);
    }
    alpha = home_font_alpha(x, y, 104, 338, &lv_font_montserrat_18, "TILT");
    if (alpha > 0) {
        return rgb565_blend(colour, white, alpha);
    }
    return colour;
}

static void ouo_set_expression(ouo_expression_t expression, int duration_ms)
{
    s_ouo_idle_visible = false;
    s_ouo_idle_epoch_us = 0;
    if (expression == OUO_EXPRESSION_DIZZY &&
        s_ouo_expression != OUO_EXPRESSION_DIZZY) {
        s_ouo_dizzy_phase = 0.0f;
        s_ouo_last_dizzy_phase_us = 0;
    } else if (expression != OUO_EXPRESSION_DIZZY) {
        s_ouo_dizzy_phase = 0.0f;
        s_ouo_last_dizzy_phase_us = 0;
    }
    s_ouo_expression = expression;
    s_ouo_expression_until_us = esp_timer_get_time() + (int64_t)duration_ms * 1000LL;
    s_ouo_dirty = true;
}

static void ouo_apply_mood_expression(int duration_ms)
{
    /* The hidden mood setting needs an immediately visible consequence when
     * returning from its page, not merely an effect on a later idle timer. */
    if (s_ouo_mood <= 33) {
        ouo_set_expression(OUO_EXPRESSION_SAD, duration_ms);
    } else if (s_ouo_mood >= 67) {
        ouo_set_expression(OUO_EXPRESSION_HAPPY, duration_ms);
    } else {
        s_ouo_expression = OUO_EXPRESSION_IDLE;
        s_ouo_expression_until_us = 0;
        s_ouo_dirty = true;
    }
}

static void ouo_open_menu(void)
{
    s_ui_screen = UI_SCREEN_OUO_MENU;
    s_ouo_menu_dirty = true;
    s_ouo_touch_active = false;
    s_ouo_menu_hold_armed = false;
    ESP_LOGI(TAG, "0u0: opened hidden menu");
}

static void ouo_handle_touch(int x, int y)
{
    /* OuO has no persistent controls.  The return area deliberately remains
     * invisible, but it must cover the rounded-screen corner that a finger
     * can actually reach rather than a tiny 48 px square. */
    if (x < 142 && y < 118) {
        s_ui_screen = UI_SCREEN_HOME;
        s_home_dirty = true;
        s_ouo_canvas_valid = false;
        ESP_LOGI(TAG, "0u0: returned home");
        return;
    }

    s_ouo_touch_active = true;
    s_ouo_touch_origin_x = x;
    s_ouo_touch_origin_y = y;
    s_ouo_last_touch_x = x;
    s_ouo_last_touch_y = y;
    s_ouo_touch_started_us = esp_timer_get_time();
    s_ouo_eye_touch_side = 0;
    /* Use the same generous visual hit region as the previewer. */
    const bool mouth_hit = ouo_in_ellipse(x, y,
                                          OUO_FACE_CENTER_X + s_ouo_gaze_x + s_ouo_shake_x,
                                          OUO_MOUTH_Y + s_ouo_gaze_y + s_ouo_shake_y,
                                          100, 70);
    /* Settings is consistently in the upper-right. Keep the established still
     * hold so an accidental face tap cannot open a modal menu. */
    s_ouo_menu_hold_armed = x >= FEATURE_SETTINGS_X1 && x < FEATURE_SETTINGS_X2 &&
                            y >= FEATURE_SETTINGS_Y1 && y < FEATURE_SETTINGS_Y2 &&
                            !mouth_hit;
    if (s_ouo_menu_hold_armed) {
        return;
    }

    const bool left_eye = ouo_eye_hit(x, y, OUO_LEFT_EYE_X + s_ouo_gaze_x,
                                      OUO_EYE_Y + s_ouo_gaze_y);
    const bool right_eye = ouo_eye_hit(x, y, OUO_RIGHT_EYE_X + s_ouo_gaze_x,
                                       OUO_EYE_Y + s_ouo_gaze_y);
    if (left_eye && !right_eye) {
        ++s_ouo_eye_pokes;
        s_ouo_mood = clamp_int(s_ouo_mood - 9, 0, 100);
        s_ouo_eye_touch_side = -1;
        ouo_set_expression(OUO_EXPRESSION_WINK_LEFT, 950);
        ESP_LOGI(TAG, "0u0: left eye poke, mood=%d", s_ouo_mood);
    } else if (right_eye && !left_eye) {
        ++s_ouo_eye_pokes;
        s_ouo_mood = clamp_int(s_ouo_mood - 9, 0, 100);
        s_ouo_eye_touch_side = 1;
        ouo_set_expression(OUO_EXPRESSION_WINK_RIGHT, 950);
        ESP_LOGI(TAG, "0u0: right eye poke, mood=%d", s_ouo_mood);
    } else if (left_eye || right_eye) {
        ++s_ouo_eye_pokes;
        s_ouo_mood = clamp_int(s_ouo_mood - 9, 0, 100);
        ouo_set_expression(OUO_EXPRESSION_SURPRISED, 950);
        ESP_LOGI(TAG, "0u0: eye poke, mood=%d", s_ouo_mood);
    } else if (y < 145 ||
               (x > OUO_LEFT_EYE_X + s_ouo_gaze_x + 66 &&
                x < OUO_RIGHT_EYE_X + s_ouo_gaze_x - 66 &&
                y < OUO_EYE_Y + s_ouo_gaze_y + 4)) {
        s_ouo_mood = clamp_int(s_ouo_mood + 8, 0, 100);
        s_ouo_eye_pokes = s_ouo_eye_pokes > 0 ? s_ouo_eye_pokes - 1 : 0;
        ouo_set_expression(OUO_EXPRESSION_HEAD_PAT, 1100);
        ESP_LOGI(TAG, "0u0: head pat, mood=%d", s_ouo_mood);
    } else if (mouth_hit) {
        /* Keep the pressed upper-dome mouth visible for the entire hold.  A
         * horizontal drag stretches that same solid shape; releasing restores
         * the quiet neutral mouth instead of leaving a modal expression. */
        s_ouo_mouth_touch_active = true;
        s_ouo_mouth_stretch_pixels = 0;
        s_ouo_mouth_x_offset = 0;
        s_ouo_mouth_y_offset = 0;
        s_ouo_mouth_side = abs(x - OUO_FACE_CENTER_X) >= 10;
        s_ouo_mouth_variant = OUO_MOUTH_VARIANT_STRETCH;
        s_ouo_mouth_variant_selected = false;
        ouo_set_expression(OUO_EXPRESSION_KISS, 950);
        ESP_LOGI(TAG, "0u0: mouth touch");
    } else if ((x < 205 || x > LCD_WIDTH - 205) && y >= 205 && y <= 390) {
        s_ouo_mood = clamp_int(s_ouo_mood + 2, 0, 100);
        s_ouo_squish_pixels = 24;
        ouo_set_expression(OUO_EXPRESSION_SQUISH, 1200);
        ESP_LOGI(TAG, "0u0: cheek squish");
    } else {
        s_ouo_mood = clamp_int(s_ouo_mood + 3, 0, 100);
        ouo_set_expression(OUO_EXPRESSION_HAPPY, 900);
        ESP_LOGI(TAG, "0u0: happy tap, mood=%d", s_ouo_mood);
    }
}

static void ouo_handle_touch_move(int x, int y)
{
    if (!s_ouo_touch_active) {
        return;
    }
    const int travel_x = x - s_ouo_touch_origin_x;
    const int travel_y = y - s_ouo_touch_origin_y;
    if (s_ouo_menu_hold_armed) {
        if (travel_x * travel_x + travel_y * travel_y >
            OUO_MENU_HOLD_TOLERANCE * OUO_MENU_HOLD_TOLERANCE) {
            s_ouo_menu_hold_armed = false;
        } else {
            return;
        }
    }
    if (s_ouo_eye_touch_side != 0) {
        /* The held eye stays closed; elapsed time alone is not surprise. */
        s_ouo_last_touch_x = x;
        s_ouo_last_touch_y = y;
        s_ouo_dirty = true;
        return;
    }
    const bool head_stroke = s_ouo_expression == OUO_EXPRESSION_HEAD_PAT;
    const bool cheek_stroke = (s_ouo_touch_origin_x < 205 ||
                               s_ouo_touch_origin_x > LCD_WIDTH - 205) &&
                              y >= 190 && y <= 390;
    if (s_ouo_mouth_touch_active) {
        /* Lock the family on the first meaningful sample.  A horizontal pull
         * from the mouth centre deliberately picks the stretch family so the
         * wide reference blob is easy to reach; starting in either side half
         * selects the captured filled-3/independent-arc side family. */
        const int ax = abs(travel_x);
        const int ay = abs(travel_y);
        if (!s_ouo_mouth_variant_selected && (ax >= 10 || ay >= 10)) {
            if (s_ouo_mouth_side && ax * 4 >= ay * 5) {
                s_ouo_mouth_variant = OUO_MOUTH_VARIANT_SIDE;
            } else if (ax * 4 >= ay * 5) {
                s_ouo_mouth_variant = OUO_MOUTH_VARIANT_STRETCH;
            } else {
                s_ouo_mouth_variant = (ouo_mouth_variant_t)(esp_random() % 4U);
            }
            s_ouo_mouth_variant_selected = true;
            ESP_LOGD(TAG, "0u0: mouth variant=%d side=%d", (int)s_ouo_mouth_variant,
                     s_ouo_mouth_side);
        }
        /* Pull the compact mouth with the finger.  The pixel renderer keeps
         * the selected family stable while its contour follows the vector. */
        s_ouo_mouth_x_offset = clamp_int(travel_x, -62, 62);
        if (s_ouo_mouth_variant == OUO_MOUTH_VARIANT_SIDE) {
            /* Side-pull uses a fixed 3+arc contour; travel moves it but does
             * not scale it with distance from the mouth centre. */
            s_ouo_mouth_stretch_pixels = 0;
            /* The captured side-pull keeps a fixed vertical contour.  Allow
             * only the previewer's small baseline drift; never move the
             * complete mouth by the full diagonal travel toward the eyes. */
            s_ouo_mouth_y_offset = clamp_int(travel_y / 8, -6, 6);
        } else {
            s_ouo_mouth_stretch_pixels = clamp_int(abs(travel_x) / 12, 0, 8);
            s_ouo_mouth_y_offset = clamp_int(travel_y, -45, 45);
        }
        ouo_set_expression(OUO_EXPRESSION_SURPRISED, 3000);
    } else if (head_stroke) {
        ouo_set_expression(OUO_EXPRESSION_HEAD_PAT, 1100);
    } else if (cheek_stroke) {
        const int inward = s_ouo_touch_origin_x < LCD_WIDTH / 2 ? travel_x : -travel_x;
        s_ouo_squish_pixels = clamp_int(20 + inward / 2, 16, 48);
        ouo_set_expression(OUO_EXPRESSION_SQUISH, 900);
    }
    s_ouo_last_touch_x = x;
    s_ouo_last_touch_y = y;
    s_ouo_dirty = true;
}

static void ouo_end_touch(void)
{
    const bool open_menu = s_ouo_menu_hold_armed &&
                           esp_timer_get_time() - s_ouo_touch_started_us >=
                               (int64_t)OUO_MENU_HOLD_MS * 1000LL;
    const bool mouth_was_held = s_ouo_mouth_touch_active;
    const bool eye_was_held = s_ouo_eye_touch_side != 0;
    const bool head_was_held = s_ouo_expression == OUO_EXPRESSION_HEAD_PAT;
    const bool mouth_was_pulled = s_ouo_mouth_variant_selected;
    s_ouo_touch_active = false;
    s_ouo_menu_hold_armed = false;
    s_ouo_mouth_touch_active = false;
    s_ouo_mouth_variant_selected = false;
    s_ouo_mouth_side = false;
    s_ouo_mouth_variant = OUO_MOUTH_VARIANT_STRETCH;
    s_ouo_eye_touch_side = 0;
    if (open_menu) {
        ouo_open_menu();
        return;
    }
    if (head_was_held) ouo_set_expression(OUO_EXPRESSION_DELIGHTED, 6000);
    else if (eye_was_held) ouo_set_expression(OUO_EXPRESSION_CARET, 700);
    else if (mouth_was_held) {
        // Release returns to a compact baseline before idle, not a smile
        // translated by the old drag offset for another half second.
        s_ouo_mouth_x_offset = 0;
        s_ouo_mouth_y_offset = 0;
        s_ouo_mouth_stretch_pixels = 0;
        ouo_set_expression(mouth_was_pulled ? OUO_EXPRESSION_MOUTH_RELEASE : OUO_EXPRESSION_KISS, 650);
    } else if (s_ouo_expression != OUO_EXPRESSION_IDLE) {
        s_ouo_expression_until_us = esp_timer_get_time() + 900000LL;
    }
}

static void ouo_handle_menu_touch(int x, int y)
{
    /* Keep the hit box on the actual drawn arrow (around 80,166), not the
     * invisible face-return zone used on the previous page. */
    if (x < 142 && y >= 112 && y < 218) {
        s_ui_screen = UI_SCREEN_OUO;
        s_ouo_canvas_valid = false;
        ouo_apply_mood_expression(2600);
        s_ouo_idle_epoch_us = 0;
        s_ouo_dirty = true;
        ESP_LOGI(TAG, "0u0 menu: returned to face");
        return;
    }
    if (y >= 208 && y <= 282) {
        const int slider_left = 86;
        const int slider_right = LCD_WIDTH - 86;
        s_ouo_mood = clamp_int((x - slider_left) * 100 /
                               (slider_right - slider_left), 0, 100);
        s_ouo_preferred_mood = (uint8_t)s_ouo_mood;
        ouo_save_preferences();
        s_ouo_eye_pokes = 0;
        ouo_apply_mood_expression(2600);
        s_ouo_menu_dirty = true;
        ESP_LOGI(TAG, "0u0 menu: mood=%d", s_ouo_mood);
    } else if (y >= 286 && y <= 331) {
        s_ouo_auto_expressions = !s_ouo_auto_expressions;
        ouo_save_preferences();
        s_ouo_idle_visible = false;
        s_ouo_idle_epoch_us = 0;
        s_ouo_menu_dirty = true;
        ESP_LOGI(TAG, "0u0 menu: auto idle=%d", s_ouo_auto_expressions);
    } else if (y >= 332 && y <= 378) {
        s_ouo_tilt_reactions = !s_ouo_tilt_reactions;
        ouo_save_preferences();
        if (!s_ouo_tilt_reactions) {
            s_ouo_gaze_x = 0;
            s_ouo_gaze_y = 0;
        }
        s_ouo_menu_dirty = true;
        ESP_LOGI(TAG, "0u0 menu: tilt reaction=%d", s_ouo_tilt_reactions);
    }
}

static void step_ouo(void)
{
    const int64_t now = esp_timer_get_time();
    if (s_ouo_touch_active && s_ouo_menu_hold_armed &&
        now - s_ouo_touch_started_us >= (int64_t)OUO_MENU_HOLD_MS * 1000LL) {
        ouo_open_menu();
        return;
    }

    /* Rotate the two dizzy spirals at a time-based rate so the animation is
     * stable across render-loop cadence and remains visible on the OLED. */
    if (s_ouo_expression == OUO_EXPRESSION_DIZZY) {
        if (s_ouo_last_dizzy_phase_us == 0) {
            s_ouo_last_dizzy_phase_us = now;
        }
        const int64_t phase_delta_us = now - s_ouo_last_dizzy_phase_us;
        if (phase_delta_us >= 16000LL) {
            /* Match the previewer's visible rotation: about 0.18 rad per
             * 16 ms frame.  The old 9e-9 coefficient advanced only 0.000144
             * rad per frame, which looked like a frozen spiral on-device. */
            s_ouo_dizzy_phase += (float)phase_delta_us * 0.00001125f;
            while (s_ouo_dizzy_phase >= 6.2831853f) {
                s_ouo_dizzy_phase -= 6.2831853f;
            }
            s_ouo_last_dizzy_phase_us = now;
            s_ouo_dirty = true;
        }
    } else {
        s_ouo_dizzy_phase = 0.0f;
        s_ouo_last_dizzy_phase_us = 0;
    }

    int gravity_x = 0;
    int gravity_y = 0;
    read_tilt(&gravity_x, &gravity_y);
    /* Use enough travel to be visible on the 1.69-inch round panel.  The
     * prior +/-38 px and two-pixel/45 ms easing made a deliberate wrist tilt
     * look almost stationary. */
    const int target_gaze_x = s_ouo_tilt_reactions ?
                              clamp_int(gravity_x * 3 / 8, -66, 66) : 0;
    const int target_gaze_y = s_ouo_tilt_reactions ?
                              clamp_int(gravity_y / 4, -44, 44) : 0;
    /* Keep the old, calm easing rate; only the visible travel was increased.
     * That makes wrist motion legible without the face darting around. */
    if (now - s_ouo_last_gaze_update_us >= 45000LL) {
        int next_x = s_ouo_gaze_x;
        int next_y = s_ouo_gaze_y;
        const int delta_x = target_gaze_x - next_x;
        const int delta_y = target_gaze_y - next_y;
        next_x += clamp_int(delta_x, -2, 2);
        next_y += clamp_int(delta_y, -2, 2);
        if (next_x != s_ouo_gaze_x || next_y != s_ouo_gaze_y) {
            s_ouo_gaze_x = next_x;
            s_ouo_gaze_y = next_y;
            s_ouo_dirty = true;
        }
        s_ouo_last_gaze_update_us = now;
    }

    const int motion_dx = gravity_x - s_ouo_last_gravity_x;
    const int motion_dy = gravity_y - s_ouo_last_gravity_y;
    const int motion = abs(motion_dx) + abs(motion_dy);
    /* A simple threshold caused a normal wrist tilt to look like a shake.
     * Count only distinct, strong direction reversals within 650 ms.  Thus a
     * deliberate left-right-left shake triggers dizzy, while picking up,
     * tilting, or putting down the pendant does not. */
    if (motion >= 22) {
        if (now - s_ouo_shake_window_started_us > 650000LL) {
            s_ouo_shake_window_started_us = now;
            s_ouo_shake_reversals = 0;
            s_ouo_last_motion_dx = 0;
            s_ouo_last_motion_dy = 0;
        }
        const int previous_motion = abs(s_ouo_last_motion_dx) +
                                    abs(s_ouo_last_motion_dy);
        const int direction_dot = motion_dx * s_ouo_last_motion_dx +
                                  motion_dy * s_ouo_last_motion_dy;
        if (previous_motion >= 22 && direction_dot <= -260) {
            ++s_ouo_shake_reversals;
        }
        s_ouo_last_motion_dx = motion_dx;
        s_ouo_last_motion_dy = motion_dy;
    } else if (now - s_ouo_shake_window_started_us > 650000LL) {
        s_ouo_shake_reversals = 0;
        s_ouo_last_motion_dx = 0;
        s_ouo_last_motion_dy = 0;
    }
    if (!s_ouo_touch_active && s_ouo_tilt_reactions && s_ouo_shake_reversals >= 2 &&
        now - s_ouo_last_shake_us >= 2800000LL) {
        s_ouo_mood = clamp_int(s_ouo_mood + 2, 0, 100);
        s_ouo_last_shake_us = now;
        s_ouo_shake_until_us = now + 1000000LL;
        ouo_set_expression(OUO_EXPRESSION_DIZZY, 1000);
        ESP_LOGI(TAG, "0u0: dizzy shake reversals=%u, motion=%d, gravity=%d/%d",
                 s_ouo_shake_reversals, motion, gravity_x, gravity_y);
        s_ouo_shake_reversals = 0;
        s_ouo_last_motion_dx = 0;
        s_ouo_last_motion_dy = 0;
    }
    s_ouo_last_gravity_x = gravity_x;
    s_ouo_last_gravity_y = gravity_y;

    /* The reference face does not need cartoon X eyes for a shake.  Its white
     * features visibly wobble together for a moment, then settle back. */
    int shake_x = 0;
    int shake_y = 0;
    if (now < s_ouo_shake_until_us) {
        switch ((int)((now / 60000LL) % 6LL)) {
        case 0: shake_x = -10; shake_y = 2; break;
        case 1: shake_x = 12;  shake_y = -3; break;
        case 2: shake_x = -8;  shake_y = 3; break;
        case 3: shake_x = 9;   shake_y = -2; break;
        case 4: shake_x = -5;  shake_y = 1; break;
        default: shake_x = 4;  shake_y = -1; break;
        }
    }
    if (shake_x != s_ouo_shake_x || shake_y != s_ouo_shake_y) {
        s_ouo_shake_x = shake_x;
        s_ouo_shake_y = shake_y;
        s_ouo_dirty = true;
    }

    if (!s_ouo_touch_active && s_ouo_squish_pixels > 0) {
        s_ouo_squish_pixels = clamp_int(s_ouo_squish_pixels - 1, 0, 48);
        s_ouo_dirty = true;
    }
    if (!s_ouo_mouth_touch_active &&
        (s_ouo_mouth_stretch_pixels != 0 || s_ouo_mouth_x_offset != 0 ||
         s_ouo_mouth_y_offset != 0)) {
        s_ouo_mouth_stretch_pixels += s_ouo_mouth_stretch_pixels > 0 ? -1 : 1;
        s_ouo_mouth_x_offset = s_ouo_mouth_x_offset > 0 ?
                                  clamp_int(s_ouo_mouth_x_offset - 2, 0, 62) :
                                  clamp_int(s_ouo_mouth_x_offset + 2, -62, 0);
        s_ouo_mouth_y_offset += s_ouo_mouth_y_offset > 0 ? -1 :
                                 (s_ouo_mouth_y_offset < 0 ? 1 : 0);
        s_ouo_dirty = true;
    }
    if (s_ouo_expression != OUO_EXPRESSION_IDLE && !s_ouo_touch_active &&
        now >= s_ouo_expression_until_us) {
        s_ouo_expression = OUO_EXPRESSION_IDLE;
        s_ouo_dirty = true;
    }
    ouo_idle_step(now);
}

static uint32_t mix32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    value *= 0x846CA68BU;
    value ^= value >> 16;
    return value;
}

static esp_err_t i2c_read(uint8_t address, uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_write_read_device(I2C_PORT, address, &reg, 1, data, length,
                                        pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t i2c_write_byte(uint8_t address, uint8_t reg, uint8_t value)
{
    uint8_t data[] = {reg, value};
    return i2c_master_write_to_device(I2C_PORT, address, data, sizeof(data),
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t i2c_write(uint8_t address, uint8_t reg,
                           const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0 || length > 15) return ESP_ERR_INVALID_ARG;
    uint8_t buffer[16];
    buffer[0] = reg;
    memcpy(buffer + 1, data, length);
    return i2c_master_write_to_device(I2C_PORT, address, buffer, length + 1,
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static int clock_bcd_to_int(uint8_t value)
{
    return ((value >> 4) & 0x0F) * 10 + (value & 0x0F);
}

static uint8_t clock_int_to_bcd(int value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static bool clock_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static bool clock_time_valid(const clock_time_t *time)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (time == NULL || time->year < 2000 || time->year > 2099 ||
        time->month < 1 || time->month > 12 || time->day < 1 ||
        time->hour < 0 || time->hour > 23 || time->minute < 0 ||
        time->minute > 59 || time->second < 0 || time->second > 59) return false;
    int maximum = days[time->month - 1];
    if (time->month == 2 && clock_is_leap_year(time->year)) maximum = 29;
    return time->day <= maximum;
}

static clock_time_t clock_read_rtc(void)
{
    uint8_t raw[7] = {0};
    clock_time_t time = {0};
    if (i2c_read(ADDR_PCF85063, 0x04, raw, sizeof(raw)) != ESP_OK ||
        (raw[0] & 0x80U) != 0) return time;
    time.second = clock_bcd_to_int(raw[0] & 0x7FU);
    time.minute = clock_bcd_to_int(raw[1] & 0x7FU);
    time.hour = clock_bcd_to_int(raw[2] & 0x3FU);
    time.day = clock_bcd_to_int(raw[3] & 0x3FU);
    time.month = clock_bcd_to_int(raw[5] & 0x1FU);
    time.year = 2000 + clock_bcd_to_int(raw[6]);
    time.valid = clock_time_valid(&time);
    return time;
}

static esp_err_t clock_write_rtc(const clock_time_t *time)
{
    if (!clock_time_valid(time)) return ESP_ERR_INVALID_ARG;
    const uint8_t raw[7] = {
        clock_int_to_bcd(time->second), clock_int_to_bcd(time->minute),
        clock_int_to_bcd(time->hour), clock_int_to_bcd(time->day), 0,
        clock_int_to_bcd(time->month), clock_int_to_bcd(time->year - 2000),
    };
    return i2c_write(ADDR_PCF85063, 0x04, raw, sizeof(raw));
}

static const char *clock_source_name(clock_source_t source)
{
    switch (source) {
        case CLOCK_SOURCE_CUSTOM: return "custom";
        case CLOCK_SOURCE_BLE: return "ble";
        case CLOCK_SOURCE_WIFI: return "wifi";
        default: return "rtc";
    }
}

static const char *clock_sync_name(clock_sync_state_t state)
{
    switch (state) {
        case CLOCK_SYNC_WAIT_WIFI: return "waiting_wifi";
        case CLOCK_SYNC_RUNNING: return "syncing";
        case CLOCK_SYNC_OK: return "ok";
        case CLOCK_SYNC_FAILED: return "failed";
        default: return "idle";
    }
}

static void clock_load_preferences(void)
{
    nvs_handle_t handle;
    if (nvs_open(CLOCK_CONFIG_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t value = 0;
    if (nvs_get_u8(handle, "source", &value) == ESP_OK && value <= CLOCK_SOURCE_WIFI)
        s_clock_source = (clock_source_t)value;
    if (nvs_get_u8(handle, "alarm_en", &value) == ESP_OK) s_alarm_enabled = value != 0;
    if (nvs_get_u8(handle, "alarm_h", &value) == ESP_OK && value < 24) s_alarm_hour = value;
    if (nvs_get_u8(handle, "alarm_m", &value) == ESP_OK && value < 60) s_alarm_minute = value;
    if (nvs_get_u8(handle, "style", &value) == ESP_OK && value < 2) s_watch_style = value;
    nvs_close(handle);
}

static void clock_save_preferences(void)
{
    nvs_handle_t handle;
    if (nvs_open(CLOCK_CONFIG_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    (void)nvs_set_u8(handle, "source", (uint8_t)s_clock_source);
    (void)nvs_set_u8(handle, "alarm_en", s_alarm_enabled ? 1 : 0);
    (void)nvs_set_u8(handle, "alarm_h", s_alarm_hour);
    (void)nvs_set_u8(handle, "alarm_m", s_alarm_minute);
    (void)nvs_set_u8(handle, "style", s_watch_style);
    (void)nvs_commit(handle);
    nvs_close(handle);
}

static bool alarm_buzzer_init(void)
{
    if (s_alarm_buzzer_ready) return true;
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_1, .freq_hz = 2000, .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel = {
        .gpio_num = PIN_SPKOUT, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1, .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_1, .duty = 0, .hpoint = 0,
    };
    const esp_err_t timer_err = ledc_timer_config(&timer);
    const esp_err_t channel_err = timer_err == ESP_OK ?
                                  ledc_channel_config(&channel) : timer_err;
    if (timer_err != ESP_OK || channel_err != ESP_OK) {
        ESP_LOGE(TAG, "Alarm: LEDC init GPIO%d timer=%s channel=%s",
                 PIN_SPKOUT, esp_err_to_name(timer_err), esp_err_to_name(channel_err));
        return false;
    }
    s_alarm_buzzer_ready = true;
    ESP_LOGI(TAG, "Alarm: buzzer ready on GPIO%d at 2000 Hz", PIN_SPKOUT);
    return true;
}

static bool alarm_set_tone(bool on)
{
    if (!s_alarm_buzzer_ready) return false;
    const esp_err_t duty_err = ledc_set_duty(
        LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, on ? 512 : 0);
    const esp_err_t update_err = duty_err == ESP_OK ?
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1) : duty_err;
    if (duty_err != ESP_OK || update_err != ESP_OK) {
        ESP_LOGE(TAG, "Alarm: tone %s failed duty=%s update=%s",
                 on ? "ON" : "OFF", esp_err_to_name(duty_err),
                 esp_err_to_name(update_err));
        return false;
    }
    s_alarm_tone_on = on;
    return true;
}

static void alarm_stop(void)
{
    (void)alarm_set_tone(false);
    s_alarm_ringing = false;
    s_watch_dirty = true;
}

static void alarm_start(TickType_t now)
{
    if (!alarm_buzzer_init()) {
        ESP_LOGE(TAG, "Alarm: buzzer init failed");
        return;
    }
    s_alarm_ringing = true;
    s_alarm_ring_deadline = now + pdMS_TO_TICKS(ALARM_RING_MS);
    s_alarm_next_tone_toggle = now + pdMS_TO_TICKS(350);
    if (!alarm_set_tone(true)) {
        s_alarm_ringing = false;
        return;
    }
    s_watch_dirty = true;
    ESP_LOGW(TAG, "Alarm: ringing at %02u:%02u", s_alarm_hour, s_alarm_minute);
}

static void clock_service(TickType_t now)
{
    static TickType_t next_rtc_check;
    if (s_clock_time_pending) {
        const clock_time_t pending = s_clock_pending_time;
        const clock_source_t source = s_clock_pending_source;
        s_clock_time_pending = false;
        const esp_err_t err = clock_write_rtc(&pending);
        if (err == ESP_OK) {
            s_clock_source = source;
            s_clock_sync_state = CLOCK_SYNC_OK;
            clock_save_preferences();
            s_watch_dirty = true;
            ESP_LOGI(TAG, "Clock: RTC set by %s", clock_source_name(source));
        } else {
            s_clock_sync_state = CLOCK_SYNC_FAILED;
            ESP_LOGE(TAG, "Clock: RTC write failed: %s", esp_err_to_name(err));
        }
    }

    if (s_clock_wifi_sync_requested) {
        if (!s_skyorb_wifi_connected) {
            s_clock_sync_state = CLOCK_SYNC_WAIT_WIFI;
        } else if (s_clock_sync_state != CLOCK_SYNC_RUNNING) {
            setenv("TZ", "CST-8", 1);
            tzset();
            esp_sntp_stop();
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            s_clock_sync_state = CLOCK_SYNC_RUNNING;
            s_clock_wifi_sync_started = now;
            ESP_LOGI(TAG, "Clock: Wi-Fi SNTP sync started");
        } else {
            const time_t epoch = time(NULL);
            struct tm local = {0};
            localtime_r(&epoch, &local);
            if (local.tm_year + 1900 >= 2024) {
                clock_time_t synced = {
                    .year = local.tm_year + 1900, .month = local.tm_mon + 1,
                    .day = local.tm_mday, .hour = local.tm_hour,
                    .minute = local.tm_min, .second = local.tm_sec, .valid = true,
                };
                s_clock_pending_time = synced;
                s_clock_pending_source = CLOCK_SOURCE_WIFI;
                s_clock_time_pending = true;
                s_clock_wifi_sync_requested = false;
                esp_sntp_stop();
            } else if ((int32_t)(now - s_clock_wifi_sync_started) >=
                       (int32_t)pdMS_TO_TICKS(15000)) {
                s_clock_wifi_sync_requested = false;
                s_clock_sync_state = CLOCK_SYNC_FAILED;
                esp_sntp_stop();
                ESP_LOGE(TAG, "Clock: Wi-Fi SNTP sync timed out");
            }
        }
    }

    if (s_alarm_ringing) {
        if ((int32_t)(now - s_alarm_ring_deadline) >= 0) alarm_stop();
        else if ((int32_t)(now - s_alarm_next_tone_toggle) >= 0) {
            (void)alarm_set_tone(!s_alarm_tone_on);
            s_alarm_next_tone_toggle = now + pdMS_TO_TICKS(350);
        }
    }
    if ((int32_t)(now - next_rtc_check) < 0) return;
    next_rtc_check = now + pdMS_TO_TICKS(1000);
    const clock_time_t current = clock_read_rtc();
    s_clock_cached_time = current;
    if (!current.valid || !s_alarm_enabled || s_alarm_ringing) return;
    const int32_t date_key = current.year * 10000 + current.month * 100 + current.day;
    /* The previous two-second trigger window could be skipped by a delayed RTC
     * sample.  The date key still guarantees at most one alarm per day, so the
     * complete matching minute is a safe and deterministic trigger window. */
    if (current.hour == s_alarm_hour && current.minute == s_alarm_minute &&
        date_key != s_alarm_last_date_key) {
        s_alarm_last_date_key = date_key;
        alarm_start(now);
    }
}

/* Keep the charge configuration with the fluid firmware, so switching away
 * from the diagnostic project cannot restore the old 4.4 V / 300 mA policy. */
static bool axp2101_update_register(uint8_t reg, uint8_t clear_mask, uint8_t set_mask)
{
    uint8_t value = 0;
    esp_err_t err = i2c_read(ADDR_AXP2101, reg, &value, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 read 0x%02X failed: %s", reg, esp_err_to_name(err));
        return false;
    }

    value = (value & (uint8_t)~clear_mask) | set_mask;
    err = i2c_write_byte(ADDR_AXP2101, reg, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 write 0x%02X failed: %s", reg, esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool configure_axp2101_charging(void)
{
    bool ok = true;

    /* No battery NTC is fitted: TS measurement must stay off.  Keep battery,
     * VBUS and VSYS ADC channels enabled for the status report. */
    ok = axp2101_update_register(0x30, 0x02, 0x0D) && ok;
    ok = axp2101_update_register(0x61, 0x03, 0x01) && ok; /* 25 mA precharge */
    ok = axp2101_update_register(0x62, 0x1F, 0x02) && ok; /* 50 mA CC */
    ok = axp2101_update_register(0x63, 0x0F, 0x11) && ok; /* 25 mA, termination on */
    ok = axp2101_update_register(0x64, 0x07, 0x03) && ok; /* 4.2 V target */

    /* Cell charger and fuel gauge on; button-cell charger off; battery detect on. */
    ok = axp2101_update_register(0x18, 0x04, 0x0A) && ok;
    ok = axp2101_update_register(0x68, 0x00, 0x01) && ok;

    ESP_LOGI(TAG, "AXP2101 charging: target=4200 mV, CC=50 mA, precharge=25 mA, termination=25 mA: %s",
             ok ? "OK" : "FAILED");
    return ok;
}

static bool axp2101_read_voltage(uint8_t high_reg, uint8_t high_mask, uint16_t *millivolts)
{
    uint8_t high = 0;
    uint8_t low = 0;
    if (i2c_read(ADDR_AXP2101, high_reg, &high, 1) != ESP_OK ||
        i2c_read(ADDR_AXP2101, high_reg + 1, &low, 1) != ESP_OK) {
        return false;
    }
    *millivolts = (uint16_t)(((high & high_mask) << 8) | low);
    return true;
}

static const char *axp2101_charge_state_name(uint8_t state)
{
    static const char *const names[] = {
        "trickle", "pre-charge", "constant-current", "constant-voltage", "done", "stopped",
    };
    return state < sizeof(names) / sizeof(names[0]) ? names[state] : "unknown";
}

static const char *axp2101_chgled_mode_name(uint8_t control)
{
    switch ((control >> 1) & 0x03) {
    case 0:
        return "Type-A";
    case 1:
        return "Type-B";
    case 2:
        return "manual";
    default:
        return "reserved";
    }
}

static const char *axp2101_chgled_manual_state_name(uint8_t control)
{
    switch ((control >> 4) & 0x03) {
    case 0:
        return "off(Hi-Z)";
    case 1:
        return "blink-1Hz";
    case 2:
        return "blink-4Hz";
    default:
        return "on(low)";
    }
}

static void log_axp2101_charge_status(void)
{
    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t percent = 0;
    uint8_t precharge = 0;
    uint8_t current = 0;
    uint8_t termination = 0;
    uint8_t target = 0;
    uint8_t charger_control = 0;
    uint8_t battery_detection = 0;
    uint8_t chgled_control = 0;
    uint16_t battery_mv = 0;
    uint16_t vbus_mv = 0;
    uint16_t vsys_mv = 0;

    bool ok = i2c_read(ADDR_AXP2101, 0x00, &status1, 1) == ESP_OK &&
              i2c_read(ADDR_AXP2101, 0x01, &status2, 1) == ESP_OK &&
              axp2101_read_voltage(0x34, 0x1F, &battery_mv) &&
              axp2101_read_voltage(0x38, 0x3F, &vbus_mv) &&
              axp2101_read_voltage(0x3A, 0x3F, &vsys_mv) &&
              i2c_read(ADDR_AXP2101, 0x61, &precharge, 1) == ESP_OK &&
              i2c_read(ADDR_AXP2101, 0x62, &current, 1) == ESP_OK &&
              i2c_read(ADDR_AXP2101, 0x63, &termination, 1) == ESP_OK &&
              i2c_read(ADDR_AXP2101, 0x64, &target, 1) == ESP_OK &&
              i2c_read(ADDR_AXP2101, 0x18, &charger_control, 1) == ESP_OK &&
              i2c_read(ADDR_AXP2101, 0x68, &battery_detection, 1) == ESP_OK &&
              i2c_read(ADDR_AXP2101, 0x69, &chgled_control, 1) == ESP_OK;
    if (!ok) {
        ESP_LOGW(TAG, "AXP2101 charge status read incomplete");
        return;
    }

    bool vbus_present = (status1 & 0x20) != 0 && (status2 & 0x08) == 0;
    bool battery_present = (status1 & 0x08) != 0;
    const bool battery_charging = vbus_present && (status2 & 0x07) <= 0x03;
    bool percent_ok = i2c_read(ADDR_AXP2101, 0xA4, &percent, 1) == ESP_OK;
    static const uint16_t target_voltage_mv[] = {0, 4000, 4100, 4200, 4350, 4400, 0, 0};
    const uint16_t target_mv = target_voltage_mv[target & 0x07];

    if (s_battery_mv != battery_mv || s_vbus_present != vbus_present ||
        s_battery_charging != battery_charging ||
        s_battery_percent_valid != percent_ok ||
        (percent_ok && s_battery_percent != percent)) {
        s_home_dirty = true;
    }
    s_battery_mv = battery_mv;
    s_vbus_present = vbus_present;
    s_battery_charging = battery_charging;
    s_battery_percent_valid = percent_ok;
    ESP_LOGI(TAG, "SD NAND: %s, mount=%s, media=%u (no automatic format/write)",
             s_sd_nand_mounted ? "mounted at /sdnand" : "not mounted",
             esp_err_to_name(s_sd_nand_mount_result),
             (unsigned)s_home_external_media_count);
    if (percent_ok) {
        s_battery_percent = percent;
    }

    ESP_LOGI(TAG, "AXP: VBUS %s %u mV | BAT %s %u mV%s | VSYS %u mV",
             vbus_present ? "present" : "absent", vbus_mv,
             battery_present ? "present" : "absent", battery_mv,
             percent_ok ? "" : " (percent unavailable)", vsys_mv);
    ESP_LOGI(TAG, "AXP: charge=%s, target=%u mV, pre=%u mA, CC=%u mA, term=%u mA (%s)",
             axp2101_charge_state_name(status2 & 0x07), target_mv,
             (precharge & 0x03) * 25U, (current & 0x1F) * 25U,
             (termination & 0x0F) * 25U, (termination & 0x10) ? "enabled" : "disabled");
    ESP_LOGI(TAG, "[LED-DIAG] reg69=0x%02X enable=%u mode=%s manual=%s | reg18=0x%02X reg68=0x%02X",
             chgled_control, chgled_control & 0x01,
             axp2101_chgled_mode_name(chgled_control),
             axp2101_chgled_manual_state_name(chgled_control),
             charger_control, battery_detection);
    if (percent_ok) {
        ESP_LOGI(TAG, "AXP: battery level=%u%%", percent);
    }
}

/* LCD colour transfers use asynchronous DMA.  The previous renderer reused
 * one stripe buffer before the preceding DMA transfer had finished, which is
 * exactly what produced the diagonal green tearing in the photographed frame. */
static bool IRAM_ATTR lcd_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                                        esp_lcd_panel_io_event_data_t *edata,
                                        void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    BaseType_t need_yield = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &need_yield);
    return need_yield == pdTRUE;
}

static esp_err_t init_i2c(void)
{
    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = 400000,
    };
    esp_err_t err = i2c_param_config(I2C_PORT, &config);
    if (err == ESP_OK) {
        err = i2c_driver_install(I2C_PORT, config.mode, 0, 0, 0);
    }
    s_i2c_ready = err == ESP_OK;
    ESP_LOGI(TAG, "I2C GPIO1/GPIO2: %s", esp_err_to_name(err));
    return err;
}

/* Mount only an already-formatted card.  In particular, do not set
 * format_if_mount_failed: U2 is also intended for user assets and a failed
 * probe must never erase it. */
static void init_sd_nand_read_only_probe(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    /* U2 is routed as a short 4-bit SDMMC bus.  The default 20 MHz mode was
     * measured at only ~1.8 MB/s for raw animation frames, so request the
     * card's standard 40 MHz high-speed mode.  This is read-only mounting;
     * it neither reformats nor changes any NAND content. */
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = PIN_SD_CLK;
    slot.cmd = PIN_SD_CMD;
    slot.d0 = PIN_SD_D0;
    slot.d1 = PIN_SD_D1;
    slot.d2 = PIN_SD_D2;
    slot.d3 = PIN_SD_D3;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
    };

    ESP_LOGI(TAG, "SD NAND: probing U2 in 4-bit SDMMC mode at %u kHz "
                  "CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d",
             (unsigned)host.max_freq_khz,
             PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_NAND_MOUNT_POINT, &host, &slot,
                                            &mount, &s_sd_nand_card);
    s_sd_nand_mount_result = err;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD NAND: not mounted (%s); no format or write was performed",
                 esp_err_to_name(err));
        return;
    }

    s_sd_nand_mounted = true;
    ESP_LOGI(TAG, "SD NAND: mounted at %s", SD_NAND_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_sd_nand_card);

    DIR *directory = opendir(SD_NAND_MOUNT_POINT);
    if (directory == NULL) {
        ESP_LOGW(TAG, "SD NAND: root opened failed");
        return;
    }
    struct dirent *entry;
    int shown = 0;
    while ((entry = readdir(directory)) != NULL && shown < 12) {
        ESP_LOGI(TAG, "SD NAND: root %s", entry->d_name);
        ++shown;
    }
    closedir(directory);
    ESP_LOGI(TAG, "SD NAND: ready for future 0u0 assets (no files written)");
}

/* The VFS helper used above owns and frees its sdmmc_card_t on unmount, so
 * MSC gets a freshly initialized descriptor.  This is intentional: it makes
 * the ownership hand-off between firmware and the PC explicit. */
static void cleanup_failed_usb_msc_start(void)
{
    if (s_usb_msc_storage_initialized) {
        tinyusb_msc_storage_deinit();
        s_usb_msc_storage_initialized = false;
    }
    if (s_usb_msc_host_initialized) {
        (void)sdmmc_host_deinit();
        s_usb_msc_host_initialized = false;
    }
    free(s_usb_msc_card);
    s_usb_msc_card = NULL;
}

static bool usb_msc_boot_requested(void)
{
    return s_usb_msc_boot_magic == USB_MSC_BOOT_MAGIC;
}

static esp_err_t start_usb_msc_mode(void)
{
    if (s_usb_msc_started) {
        return ESP_OK;
    }

    esp_err_t err = ESP_OK;
    if (s_sd_nand_mounted && s_sd_nand_card != NULL) {
        /* Legacy live handoff support: stop every local filesystem access
         * before exposing raw blocks to USB.  New UI requests reboot into the
         * dedicated path below, before BLE and media tasks are started. */
        s_home_media_io_enabled = false;
        for (int retry = 0; s_home_media_reader_busy && retry < 30; ++retry) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_home_media_reader_busy) {
            s_home_media_io_enabled = true;
            ESP_LOGE(TAG, "USB disk: media reader did not release U2 NAND");
            return ESP_ERR_TIMEOUT;
        }
        err = esp_vfs_fat_sdcard_unmount(SD_NAND_MOUNT_POINT, s_sd_nand_card);
        if (err != ESP_OK) {
            s_home_media_io_enabled = true;
            ESP_LOGE(TAG, "USB disk: cannot release U2 NAND: %s", esp_err_to_name(err));
            return err;
        }
        s_sd_nand_mounted = false;
        s_sd_nand_card = NULL;
    } else if (!usb_msc_boot_requested()) {
        ESP_LOGE(TAG, "USB disk: no mounted VFS or dedicated boot request");
        return ESP_ERR_INVALID_STATE;
    }

    s_usb_msc_card = calloc(1, sizeof(*s_usb_msc_card));
    if (s_usb_msc_card == NULL) {
        return ESP_ERR_NO_MEM;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = PIN_SD_CLK;
    slot.cmd = PIN_SD_CMD;
    slot.d0 = PIN_SD_D0;
    slot.d1 = PIN_SD_D1;
    slot.d2 = PIN_SD_D2;
    slot.d3 = PIN_SD_D3;

    err = host.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB disk: SDMMC host init failed: %s", esp_err_to_name(err));
        cleanup_failed_usb_msc_start();
        return err;
    }
    s_usb_msc_host_initialized = true;

    err = sdmmc_host_init_slot(host.slot, &slot);
    if (err == ESP_OK) {
        err = sdmmc_card_init(&host, s_usb_msc_card);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB disk: U2 re-probe for MSC failed: %s", esp_err_to_name(err));
        cleanup_failed_usb_msc_start();
        return err;
    }

    const tinyusb_msc_sdmmc_config_t storage_config = {
        .card = s_usb_msc_card,
        .mount_config = {
            .format_if_mount_failed = false,
            .max_files = 4,
            .allocation_unit_size = 16 * 1024,
        },
    };
    err = tinyusb_msc_storage_init_sdmmc(&storage_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB disk: MSC storage init failed: %s", esp_err_to_name(err));
        cleanup_failed_usb_msc_start();
        return err;
    }
    s_usb_msc_storage_initialized = true;

    /* NULL descriptors select the Kconfig-generated MSC-only descriptor.
     * The normal firmware starts as USB Serial/JTAG; after this call Windows
     * re-enumerates the same native USB data pair as a removable disk. */
    const tinyusb_config_t usb_config = {0};
    err = tinyusb_driver_install(&usb_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB disk: TinyUSB install failed: %s", esp_err_to_name(err));
        cleanup_failed_usb_msc_start();
        return err;
    }

    s_usb_msc_started = true;
    s_usb_msc_boot_magic = 0;
    ESP_LOGW(TAG, "USB disk: U2 NAND is now owned by the PC. Safely eject it, then tap "
                  "OFF on PACON to return to normal app mode.");
    return ESP_OK;
}

static esp_err_t init_lcd(void)
{
    gpio_config_t power_config = {
        .pin_bit_mask = 1ULL << PIN_LCD_POWER,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&power_config);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_level(PIN_LCD_POWER, 1);
    if (err != ESP_OK) {
        return err;
    }
    /* The PMIC rail is configured before this function is entered.  Give the
     * panel's local supply and reset capacitor a defined settling window while
     * brightness is still 0, rather than lighting OLED pixels at power edge. */
    vTaskDelay(pdMS_TO_TICKS(20));

    spi_bus_config_t bus_config = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_CLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
        LCD_STRIPE_BYTES);
    err = spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    /* Two independent DMA stripes let the CPU compose the next 64 rows while
     * QSPI sends the previous rows.  A counting semaphore tracks each queued
     * completion so neither buffer is reused too early. */
    s_lcd_done = xSemaphoreCreateCounting(2, 0);
    if (s_lcd_done == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    io_config.trans_queue_depth = 2;
    io_config.on_color_trans_done = lcd_transfer_done;
    io_config.user_ctx = s_lcd_done;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_lcd_io);
    if (err != ESP_OK) {
        return err;
    }

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    err = esp_lcd_new_panel_sh8601(s_lcd_io, &panel_config, &s_lcd_panel);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_lcd_panel_reset(s_lcd_panel);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_lcd_panel_init(s_lcd_panel);
    if (err != ESP_OK) {
        return err;
    }
    /* Keep the panel dark until its old GRAM contents have been replaced by a
     * complete black frame.  Otherwise a warm restart exposes a green scan as
     * the controller refreshes stale rows from top to bottom. */
    err = lcd_set_brightness(0);
    if (err != ESP_OK) {
        return err;
    }

    /* Keep the persistent working image in PSRAM, but stage every outgoing
     * QSPI transfer in internal DMA memory.  Sending PSRAM directly previously
     * caused unstable colour streams on this board. */
    s_lcd_stripe = heap_caps_aligned_calloc(64, 1, LCD_STRIPE_BYTES,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA |
                                            MALLOC_CAP_8BIT);
    if (s_lcd_stripe == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_lcd_stripe_secondary = heap_caps_aligned_calloc(
        64, 1, LCD_STRIPE_BYTES,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (s_lcd_stripe_secondary == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_lcd_canvas = heap_caps_aligned_calloc(64, 1, LCD_FRAME_BYTES,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lcd_canvas == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (int stripe_y = 0; stripe_y < LCD_HEIGHT; stripe_y += LCD_STRIPE_LINES) {
        const int stripe_end = clamp_int(stripe_y + LCD_STRIPE_LINES, 0, LCD_HEIGHT);
        err = esp_lcd_panel_draw_bitmap(s_lcd_panel, 0, stripe_y,
                                        LCD_WIDTH, stripe_end, s_lcd_stripe);
        if (err != ESP_OK ||
            xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
            return err != ESP_OK ? err : ESP_ERR_TIMEOUT;
        }
    }
    err = esp_lcd_panel_disp_on_off(s_lcd_panel, true);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    err = lcd_set_brightness(s_user_brightness);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "LCD canvas PSRAM=%u B; pipelined DMA stripes=2 x %d x %d",
             (unsigned)LCD_FRAME_BYTES, LCD_WIDTH, LCD_STRIPE_LINES);
    return ESP_OK;
}

static void init_touch(void)
{
    if (!s_i2c_ready) {
        return;
    }
    gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << PIN_TOUCH_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    if (gpio_config(&reset_config) != ESP_OK) {
        return;
    }
    (void)gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    (void)gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t mode = 0;
    s_touch_ready = i2c_read(ADDR_TOUCH, 0x00, &mode, 1) == ESP_OK;
    ESP_LOGI(TAG, "FT3168 touch: %s", s_touch_ready ? "ready" : "not available");
}

static void init_imu(void)
{
    if (!s_i2c_ready) {
        return;
    }
    uint8_t id = 0;
    if (i2c_read(ADDR_QMI8658, 0x00, &id, 1) != ESP_OK || id != 0x05) {
        ESP_LOGW(TAG, "QMI8658 unavailable (ID=0x%02X); use autonomous motion.", id);
        return;
    }

    /* 8 g range, 250 Hz output rate, accelerometer only. */
    esp_err_t err = i2c_write_byte(ADDR_QMI8658, 0x08, 0x00);
    if (err == ESP_OK) {
        err = i2c_write_byte(ADDR_QMI8658, 0x02, 0x60);
    }
    if (err == ESP_OK) {
        err = i2c_write_byte(ADDR_QMI8658, 0x03, 0x25);
    }
    if (err == ESP_OK) {
        err = i2c_write_byte(ADDR_QMI8658, 0x08, 0x01);
    }
    s_imu_ready = err == ESP_OK;
    ESP_LOGI(TAG, "QMI8658 tilt control: %s", esp_err_to_name(err));
}

/* The board now uses the 0x47 AXP2101 variant, while some parts report 0x4A. */
static void probe_axp2101(void)
{
    if (!s_i2c_ready) {
        return;
    }
    uint8_t chip_id = 0;
    uint8_t status = 0;
    esp_err_t id_err = i2c_read(ADDR_AXP2101, 0x03, &chip_id, 1);
    esp_err_t status_err = i2c_read(ADDR_AXP2101, 0x00, &status, 1);
    if (id_err == ESP_OK && (chip_id == AXP2101_CHIP_ID || chip_id == AXP2101_CHIP_ID2)) {
        s_axp2101_ready = true;
        ESP_LOGI(TAG, "AXP2101: PASS (ID=0x%02X, STATUS1=0x%02X, status=%s)",
                 chip_id, status, esp_err_to_name(status_err));
        bool configured = false;
        for (unsigned attempt = 1; attempt <= 3 && !configured; ++attempt) {
            configured = configure_axp2101_charging();
            if (!configured) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        log_axp2101_charge_status();
    } else {
        ESP_LOGW(TAG, "AXP2101: unavailable (read=%s, ID=0x%02X, status=%s)",
                 esp_err_to_name(id_err), chip_id, esp_err_to_name(status_err));
    }
}

static void read_tilt(int *gravity_x, int *gravity_y)
{
    *gravity_x = 0;
    *gravity_y = 0;
    if (!s_imu_ready) {
        return;
    }

    uint8_t raw[6] = {0};
    if (i2c_read(ADDR_QMI8658, 0x35, raw, sizeof(raw)) != ESP_OK) {
        return;
    }
    int16_t accel_x = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);
    int16_t accel_y = (int16_t)(((uint16_t)raw[3] << 8) | raw[2]);

    /* Q8 acceleration: deliberately lively, about 0.7 pixel/frame^2 at a
     * strong tilt.  The prior 0.3 pixel/frame^2 setting felt inert in hand. */
    *gravity_x = clamp_int(-(int)accel_y / 48, -176, 176);
    *gravity_y = clamp_int((int)accel_x / 48, -176, 176);
}

static void enter_fluid_screen(void)
{
    s_ui_screen = UI_SCREEN_FLUID;
    s_fluid_canvas_valid = false;
    s_apps_canvas_valid = false;
    s_apps_home_transition_frame = NULL;
    block_touch_until_release();
    ESP_LOGI(TAG, "Apps: entered Fluid");
}

static void enter_ouo_screen(void)
{
    s_ui_screen = UI_SCREEN_OUO;
    s_ouo_dirty = true;
    s_ouo_canvas_valid = false;
    s_ouo_menu_dirty = false;
    s_ouo_expression = OUO_EXPRESSION_IDLE;
    s_ouo_gaze_x = 0;
    s_ouo_gaze_y = 0;
    s_ouo_squish_pixels = 0;
    s_ouo_mouth_stretch_pixels = 0;
    s_ouo_mouth_x_offset = 0;
    s_ouo_mouth_y_offset = 0;
    s_ouo_mouth_variant = OUO_MOUTH_VARIANT_STRETCH;
    s_ouo_mouth_variant_selected = false;
    s_ouo_mouth_side = false;
    s_ouo_shake_x = 0;
    s_ouo_shake_y = 0;
    s_ouo_shake_until_us = 0;
    s_ouo_last_shake_us = 0;
    s_ouo_dizzy_phase = 0.0f;
    s_ouo_last_dizzy_phase_us = 0;
    s_ouo_last_motion_dx = 0;
    s_ouo_last_motion_dy = 0;
    s_ouo_shake_reversals = 0;
    s_ouo_shake_window_started_us = 0;
    s_ouo_mouth_touch_active = false;
    s_ouo_touch_active = false;
    s_ouo_menu_hold_armed = false;
    s_ouo_last_gravity_x = 0;
    s_ouo_last_gravity_y = 0;
    s_ouo_last_gaze_update_us = 0;
    s_ouo_idle_epoch_us = 0;
    s_ouo_idle_visible = false;
    if (s_ouo_idle_tiles == NULL) {
        s_ouo_idle_tiles = heap_caps_malloc(OUO_IDLE_TILE_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_ouo_idle_tiles == NULL) s_ouo_idle_tiles = heap_caps_malloc(OUO_IDLE_TILE_PIXELS, MALLOC_CAP_8BIT);
        if (s_ouo_idle_tiles == NULL) ESP_LOGW(TAG, "0u0 idle: no buffer; using static face");
    }
    s_apps_canvas_valid = false;
    s_apps_home_transition_frame = NULL;
    block_touch_until_release();
    ESP_LOGI(TAG, "Apps: entered 0u0");
}

static const char *ble_ui_screen_name(ui_screen_t screen)
{
    switch (screen) {
    case UI_SCREEN_HOME: return "HOME";
    case UI_SCREEN_FLUID: return "FLUID";
    case UI_SCREEN_OUO: return "OUO";
    default: return "OTHER";
    }
}

static void apply_ble_ui_request(void)
{
    ui_screen_t target;
    if (s_ble_ui_requests != NULL && xQueueReceive(s_ble_ui_requests, &target, 0) == pdTRUE) {
        bool busy = s_ble_media_transfer_active || s_usb_msc_started || s_usb_msc_start_requested;
        if (!busy) {
            if (s_ui_screen == UI_SCREEN_MIC_TEST) pacon_mic_close();
            /* Clear in-flight local gestures before entering the requested
             * screen, but preserve all user settings and uploaded media. */
            s_home_slide_active = false;
            s_home_slide_waiting = false;
            s_apps_dismiss_active = false;
            s_apps_entrance_active = false;
            s_apps_canvas_valid = false;
            s_apps_home_transition_frame = NULL;
            s_ouo_touch_active = false;
            s_ouo_mouth_touch_active = false;
            s_ouo_menu_hold_armed = false;
            s_ouo_canvas_valid = false;
            s_settings_touch_dragging = false;
            s_touch_energy = 0;
            if (target == UI_SCREEN_FLUID) enter_fluid_screen();
            else if (target == UI_SCREEN_OUO) enter_ouo_screen();
            else {
                s_ui_screen = UI_SCREEN_HOME;
                s_home_dirty = true;
                block_touch_until_release();
            }
            display_note_activity();
            ESP_LOGI(TAG, "BLE UI: switched to %s", ble_ui_screen_name(target));
        }
        portENTER_CRITICAL(&s_ble_ui_lock);
        s_ble_ui_snapshot = s_ui_screen;
        s_ble_ui_state = busy ? BLE_UI_BUSY : BLE_UI_DONE;
        portEXIT_CRITICAL(&s_ble_ui_lock);
    } else {
        portENTER_CRITICAL(&s_ble_ui_lock);
        s_ble_ui_snapshot = s_ui_screen;
        portEXIT_CRITICAL(&s_ble_ui_lock);
    }
}

static void enter_usb_disk_screen(void)
{
    s_ui_screen = UI_SCREEN_USB_DISK;
    s_usb_disk_dirty = true;
    /* Merely opening this page must not re-enumerate USB and close the serial
     * monitor.  The user explicitly enables MSC with the on-screen switch. */
    s_usb_msc_start_requested = false;
    s_usb_msc_exit_requested = false;
    s_apps_canvas_valid = false;
    s_apps_home_transition_frame = NULL;
    block_touch_until_release();
    ESP_LOGI(TAG, "Apps: entered USB disk controls; serial remains active");
}

static void block_touch_until_release(void)
{
    s_touch_blocked_until_release = true;
    s_touch_down = true;
    s_home_pull_armed = false;
    s_home_swipe_handled = true;
    s_apps_swipe_handled = true;
}

static void begin_apps_dismiss(void)
{
    if (s_apps_dismiss_active || s_apps_entrance_active) {
        return;
    }
    s_apps_dismiss_active = true;
    s_apps_dismiss_step = 0;
    s_apps_entrance_step = 0;
    s_apps_swipe_handled = true;
    s_apps_dirty = true;
    block_touch_until_release();
    ESP_LOGI(TAG, "Apps: upward swipe, starting return animation");
}

static bool fluid_colour_picker_select(int x, int y)
{
    const int dx = x - LCD_WIDTH / 2;
    const int dy = y - 270;
    const int distance2 = dx * dx + dy * dy;
    if (distance2 > 150 * 150) return false;
    float hue = atan2f((float)dy, (float)dx) / 6.283185307f;
    if (hue < 0.0f) hue += 1.0f;
    float saturation = sqrtf((float)distance2) / 150.0f;
    if (saturation < 0.15f) saturation = 0.15f;
    if (!s_custom_palette_active || hue != s_colour_hue || saturation != s_colour_saturation) {
        set_custom_palette(hue, saturation);
        fluid_queue_preferences();
        s_colour_picker_dirty = true;
        s_fluid_canvas_valid = false;
    }
    return true;
}

static void fluid_colour_picker_touch(int x, int y)
{
    if (!s_touch_down) {
        if (y < 106) {
            s_ui_screen = UI_SCREEN_FLUID_SETTINGS;
            s_settings_dirty = true;
            s_colour_dragging = false;
            block_touch_until_release();
            return;
        }
        s_colour_dragging = fluid_colour_picker_select(x, y);
    } else if (s_colour_dragging) {
        /* A drag that began inside the wheel stays a colour gesture, even
         * over the back button. Leaving/re-entering the wheel is safe. */
        (void)fluid_colour_picker_select(x, y);
    }
    s_touch_down = true;
}

static void poll_touch(void)
{
    if (!s_touch_ready) {
        return;
    }
    uint8_t count = 0;
    if (i2c_read(ADDR_TOUCH, 0x02, &count, 1) != ESP_OK) {
        return;
    }
    const uint8_t touch_count = count & 0x0F;
    if (touch_count == 0) {
        s_colour_dragging = false;
        s_display_wake_touch_suppressed = false;
        if (s_ui_screen == UI_SCREEN_OUO && s_touch_down) {
            ouo_end_touch();
        }
        if (s_ui_screen == UI_SCREEN_WIFI_SETTINGS && s_touch_down &&
            !s_touch_blocked_until_release) {
            wifi_settings_handle_release();
        }
        if (s_ui_screen == UI_SCREEN_SKYORB && s_touch_down &&
            !s_touch_blocked_until_release) {
            skyorb_handle_touch_release();
        }
        s_touch_down = false;
        s_touch_blocked_until_release = false;
        return;
    }

    /* Only the primary contact is consumed.  The current PACON interaction
     * model deliberately stays single-pointer; a second reported contact is
     * ignored instead of entering a separate squeeze state. */
    uint8_t point[10] = {0};
    if (i2c_read(ADDR_TOUCH, 0x03, point, sizeof(point)) != ESP_OK) {
        return;
    }
    int x = ((point[0] & 0x0F) << 8) | point[1];
    int y = ((point[2] & 0x0F) << 8) | point[3];
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) {
        return;
    }

    if (s_display_sleeping) {
        display_note_activity();
        s_display_wake_touch_suppressed = true;
        s_touch_down = true;
        return;
    }
    if (s_display_wake_touch_suppressed) {
        s_touch_down = true;
        return;
    }
    display_note_activity();

    /* Page transitions are edge-triggered.  Ignore all samples from the
     * contact that caused the transition and wait for FT3168 to report zero
     * contacts before arming another page gesture. */
    if (s_touch_blocked_until_release) {
        s_touch_down = true;
        return;
    }

    if (s_ui_screen == UI_SCREEN_OUO) {
        if (!s_touch_down) {
            ouo_handle_touch(x, y);
        } else {
            ouo_handle_touch_move(x, y);
        }
        s_touch_down = true;
        return;
    }

    if (s_ui_screen == UI_SCREEN_OUO_MENU) {
        if (!s_touch_down) {
            ouo_handle_menu_touch(x, y);
        }
        s_touch_down = true;
        return;
    }

    if (s_ui_screen == UI_SCREEN_HOME) {
        if (!s_touch_down) {
            s_home_pull_origin_x = x;
            s_home_pull_origin_y = y;
            s_home_pull_armed = y <= 118;
            s_home_swipe_handled = false;
        } else if (!s_home_swipe_handled &&
                   abs(x - s_home_pull_origin_x) >= 70 &&
                   abs(x - s_home_pull_origin_x) > abs(y - s_home_pull_origin_y) + 18) {
            const uint8_t media_count = home_media_count();
            if (media_count > 1) {
                const uint8_t from = s_home_media_index;
                const int8_t direction = x < s_home_pull_origin_x ? -1 : 1;
                const uint8_t to = (uint8_t)((from +
                                    (direction < 0 ? 1 : media_count - 1)) % media_count);
                /* Capture the first swipe immediately.  NAND preloading then
                 * completes in a worker task rather than blocking this touch
                 * loop for a full 442,700-byte frame read. */
                s_home_pending_slide_from = from;
                s_home_pending_slide_to = to;
                s_home_pending_slide_from_frame = s_home_animation_frame;
                s_home_pending_slide_direction = direction;
                s_home_slide_waiting = true;
                if (!home_begin_pending_media_transition(xTaskGetTickCount())) {
                    ESP_LOGI(TAG, "Media: captured swipe; loading item %u/%u in background",
                             (unsigned)(to + 1), (unsigned)media_count);
                }
            }
            s_home_pull_armed = false;
            s_home_swipe_handled = true;
        } else if (s_home_pull_armed &&
                   y - s_home_pull_origin_y >= 66 &&
                   abs(x - s_home_pull_origin_x) <= 126) {
            s_ui_screen = UI_SCREEN_APPS;
            s_apps_dirty = true;
            s_apps_dismiss_active = false;
            s_apps_dismiss_step = 0;
            s_apps_entrance_active = true;
            s_apps_entrance_step = 0;
            s_apps_swipe_handled = false;
            s_apps_canvas_valid = false;
            prepare_home_labels();
            s_apps_home_transition_frame = home_cached_frame_for_index(s_home_media_index);
            s_home_pull_armed = false;
            block_touch_until_release();
            ESP_LOGI(TAG, "Media: pulled down to app launcher");
        }
        s_touch_down = true;
        return;
    }

    if (s_ui_screen == UI_SCREEN_APPS) {
        if (s_apps_dismiss_active || s_apps_entrance_active) {
            /* Finish the current page transition before accepting another
             * launch or gesture.  This keeps a fast second touch from
             * snapping the animation back to a static launcher. */
            block_touch_until_release();
            return;
        }
        if (!s_touch_down) {
            s_apps_swipe_origin_x = x;
            s_apps_swipe_origin_y = y;
            s_apps_swipe_handled = false;
            if (home_in_circle(x, y, 142, 145, 44)) {
                enter_fluid_screen();
            } else if (home_in_circle(x, y, 333, 145, 44)) {
                enter_ouo_screen();
            } else if (home_in_circle(x, y, LCD_WIDTH / 2, 145, 44)) {
                watch_enter();
            } else if (home_in_circle(x, y, 142, 265, 44)) {
                skyorb_enter();
            } else if (home_in_circle(x, y, 333, 265, 44)) {
                enter_usb_disk_screen();
            } else if (home_in_circle(x, y, LCD_WIDTH / 2, 265, 44)) {
                settings_enter();
            } else if (home_in_circle(x, y, LCD_WIDTH / 2, 385, 44)) {
                camera_enter();
            } else if (y >= 438) {
                begin_apps_dismiss();
            }
        } else if (!s_apps_swipe_handled &&
                   s_apps_swipe_origin_y - y >= 66 &&
                   abs(x - s_apps_swipe_origin_x) <= 126) {
            begin_apps_dismiss();
        }
        s_touch_down = true;
        return;
    }

    if (s_ui_screen == UI_SCREEN_USB_DISK) {
        if (!s_touch_down && home_in_round_rect(x, y, 155, 350, 320, 432, 24)) {
            if (s_usb_msc_started) {
                if (!s_usb_msc_exit_armed) {
                    /* FT3168 can retain the ON contact across the short
                     * reboot.  It must report a real release, followed by the
                     * arming delay, before a new touch may request OFF. */
                    block_touch_until_release();
                    return;
                }
                /* The PC must have ejected the volume first.  A reboot is the
                 * only safe ownership hand-off back to the firmware VFS. */
                s_usb_msc_exit_requested = true;
                ESP_LOGW(TAG, "USB disk: OFF requested; restarting into serial/app mode");
            } else {
                s_usb_msc_result = ESP_ERR_INVALID_STATE;
                /* Acknowledge the first accepted tap before the intentional
                 * reboot; otherwise the control appears dead until MSC boot. */
                s_usb_msc_ui_on = true;
                s_usb_disk_dirty = true;
                s_usb_msc_boot_magic = USB_MSC_BOOT_MAGIC;
                s_usb_msc_reboot_requested = true;
                ESP_LOGW(TAG, "USB disk: ON requested; restarting into dedicated MSC mode");
            }
            block_touch_until_release();
            return;
        }
        if (!s_touch_down && !s_usb_msc_started && x < 104 && y < 104) {
            s_ui_screen = UI_SCREEN_APPS;
            s_apps_dirty = true;
            block_touch_until_release();
            ESP_LOGI(TAG, "USB disk: returned without changing USB mode");
            return;
        }
        s_touch_down = true;
        return;
    }

    if (s_ui_screen == UI_SCREEN_SKYORB) {
        if (!s_touch_down) {
            skyorb_handle_touch(x, y);
        } else {
            skyorb_handle_touch_move(x, y);
        }
        s_touch_down = true;
        return;
    }

    if (s_ui_screen == UI_SCREEN_WATCH) {
        if (!s_touch_down) {
            watch_handle_touch(x, y);
        }
        s_touch_down = true;
        return;
    }

    if (s_ui_screen == UI_SCREEN_CAMERA) {
        if (!s_touch_down) camera_handle_touch(x, y);
        s_touch_down = true;
        return;
    }

    if (s_ui_screen == UI_SCREEN_MIC_TEST) {
        if (!s_touch_down) mic_test_handle_touch(x, y);
        s_touch_down = true;
        return;
    }

    if (s_ui_screen == UI_SCREEN_SETTINGS) {
        if (!s_touch_down) {
            s_settings_touch_origin_y = y;
            s_settings_touch_last_y = y;
            s_settings_touch_dragging = false;
            settings_handle_touch(x, y);
        } else {
            const int delta = y - s_settings_touch_last_y;
            if (abs(y - s_settings_touch_origin_y) >= 12) {
                s_settings_touch_dragging = true;
            }
            if (s_settings_touch_dragging && delta != 0) {
                s_settings_scroll_y = clamp_int(s_settings_scroll_y - delta,
                                                0, SETTINGS_SCROLL_MAX);
                s_device_settings_dirty = true;
            }
            s_settings_touch_last_y = y;
        }
        s_touch_down = true;
        return;
    }

    if (s_ui_screen == UI_SCREEN_WIFI_SETTINGS) {
        if (!s_touch_down) {
            wifi_settings_handle_touch(x, y);
        } else {
            wifi_settings_update_touch(x, y);
        }
        s_touch_down = true;
        return;
    }

    /* Fluid navigation is invisible but its original touch rectangles remain
     * active on the first press. Display-wake suppression above is unchanged. */
    if (s_ui_screen == UI_SCREEN_COLOUR_PICKER) {
        fluid_colour_picker_touch(x, y);
        return;
    }
    if (!s_touch_down) {
        if (s_ui_screen == UI_SCREEN_FLUID_SETTINGS) {
            if (y < 92) {
                s_ui_screen = UI_SCREEN_FLUID;
                s_fluid_canvas_valid = false;
                ESP_LOGI(TAG, "Fluid settings: returned to fluid");
            } else if (y >= 112 && y < 172) {
                s_fluid_shape = FLUID_SHAPE_SIMPLE;
                fluid_queue_preferences();
                s_settings_dirty = true;
                s_fluid_canvas_valid = false;
                ESP_LOGI(TAG, "Fluid shape: simple");
            } else if (y >= 182 && y < 242) {
                s_fluid_shape = FLUID_SHAPE_BLOCKS;
                fluid_queue_preferences();
                s_settings_dirty = true;
                s_fluid_canvas_valid = false;
                ESP_LOGI(TAG, "Fluid shape: blocks");
            } else if (y >= 252 && y < 312) {
                s_fluid_shape = FLUID_SHAPE_MATRIX;
                fluid_queue_preferences();
                s_settings_dirty = true;
                s_fluid_canvas_valid = false;
                ESP_LOGI(TAG, "Fluid shape: matrix");
            } else if (y >= 338 && y <= 430) {
                s_ui_screen = UI_SCREEN_COLOUR_PICKER;
                s_colour_picker_dirty = true;
                block_touch_until_release();
                ESP_LOGI(TAG, "Fluid settings: opened colour palette");
            }
        } else if (y >= 68 && y < 125 && x >= 55 && x < 215) {
            s_ui_screen = UI_SCREEN_HOME;
            s_home_dirty = true;
            ESP_LOGI(TAG, "Fluid: returned home");
        } else if (x >= FEATURE_SETTINGS_X1 && x < FEATURE_SETTINGS_X2 &&
                   y >= FEATURE_SETTINGS_Y1 && y < FEATURE_SETTINGS_Y2) {
            s_ui_screen = UI_SCREEN_FLUID_SETTINGS;
            s_settings_dirty = true;
            ESP_LOGI(TAG, "Fluid: opened settings");
        } else {
            s_touch_x = x;
            s_touch_y = y;
            s_touch_energy = 255;
            /* Colour is exclusively selected through the palette screen.
             * A direct touch now affects only the liquid physics. */
            ESP_LOGI(TAG, "liquid impulse x=%d y=%d", x, y);
        }
    }
    s_touch_down = true;
}

static void init_particles(void)
{
    /* Opal simple mode starts as a broad, shallow liquid sheet.  Its original
     * 11-pixel spacing would violate PACON's deliberately larger no-overlap
     * distance, so choose the widest row count that fits this display at the
     * safe spacing instead. */
    const int spacing = PARTICLE_MIN_DISTANCE + 2;
    const int columns = clamp_int((LCD_WIDTH - PARTICLE_DRAW_RADIUS * 2) / spacing + 1,
                                  1, PARTICLE_COUNT);
    const int span_x = (columns - 1) * spacing;
    const int start_x = (LCD_WIDTH - span_x) / 2;
    const int start_y = PARTICLE_INIT_TOP;

    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        uint32_t random = mix32((uint32_t)i + 0xB5297A4DU);
        int column = i % columns;
        int row = i / columns;
        int jitter_x = (int)(random & 0x03) - 1;
        int jitter_y = (int)((random >> 8) & 0x03) - 1;
        int x = start_x + column * spacing + jitter_x;
        int y = start_y + row * spacing + jitter_y;
        s_particles[i] = (fluid_particle_t){
            .x = Q8(x),
            .y = Q8(y),
            .vx = ((int32_t)((random >> 16) & 0x1F) - 15) * 14,
            .vy = ((int32_t)((random >> 21) & 0x1F) - 15) * 14,
        };
    }
}

static int hash_cell_for(const fluid_particle_t *particle)
{
    int x = clamp_int((int)(particle->x >> 8) / HASH_CELL_SIZE, 0, HASH_COLUMNS - 1);
    int y = clamp_int((int)(particle->y >> 8) / HASH_CELL_SIZE, 0, HASH_ROWS - 1);
    return y * HASH_COLUMNS + x;
}

static void rebuild_hash(void)
{
    memset(s_hash_count, 0, sizeof(s_hash_count));
    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        ++s_hash_count[hash_cell_for(&s_particles[i])];
    }
    s_hash_start[0] = 0;
    for (int i = 0; i < HASH_CELLS; ++i) {
        s_hash_start[i + 1] = s_hash_start[i] + s_hash_count[i];
        s_hash_cursor[i] = s_hash_start[i];
    }
    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        int cell = hash_cell_for(&s_particles[i]);
        s_hash_particle[s_hash_cursor[cell]++] = i;
    }
}

static void constrain_to_bowl(fluid_particle_t *particle)
{
    const float center_x = LCD_WIDTH / 2.0f;
    const float center_y = LCD_HEIGHT / 2.0f;
    const float radius = LCD_HEIGHT / 2.0f - PARTICLE_DRAW_RADIUS - 8.0f;
    float x = (float)particle->x / 256.0f;
    float y = (float)particle->y / 256.0f;
    float dx = x - center_x;
    float dy = y - center_y;
    float distance2 = dx * dx + dy * dy;
    float limit2 = radius * radius;
    if (distance2 <= limit2) {
        return;
    }

    float distance = sqrtf(distance2);
    float nx = dx / distance;
    float ny = dy / distance;
    x = center_x + nx * radius;
    y = center_y + ny * radius;
    particle->x = (int32_t)(x * 256.0f);
    particle->y = (int32_t)(y * 256.0f);

    float vx = (float)particle->vx / 256.0f;
    float vy = (float)particle->vy / 256.0f;
    float outward_speed = vx * nx + vy * ny;
    if (outward_speed > 0.0f) {
        /* Opal simple mode: lively but visibly lossy wall rebound. */
        vx -= outward_speed * 1.36f * nx;
        vy -= outward_speed * 1.36f * ny;
        particle->vx = (int32_t)(vx * 256.0f);
        particle->vy = (int32_t)(vy * 256.0f);
    }
}

static void separate_particles(void)
{
    const int min_distance2 = PARTICLE_MIN_DISTANCE * PARTICLE_MIN_DISTANCE;
    /* A dense pile needs a second projection pass.  One pass can leave a
     * collision chain unresolved, which is visible as droplets overlapping. */
    for (int iteration = 0; iteration < 2; ++iteration) {
        rebuild_hash();
        for (int i = 0; i < PARTICLE_COUNT; ++i) {
            fluid_particle_t *a = &s_particles[i];
            int cell = hash_cell_for(a);
            int cell_x = cell % HASH_COLUMNS;
            int cell_y = cell / HASH_COLUMNS;
            for (int y = clamp_int(cell_y - 1, 0, HASH_ROWS - 1);
                 y <= clamp_int(cell_y + 1, 0, HASH_ROWS - 1); ++y) {
                for (int x = clamp_int(cell_x - 1, 0, HASH_COLUMNS - 1);
                     x <= clamp_int(cell_x + 1, 0, HASH_COLUMNS - 1); ++x) {
                    int neighbour = y * HASH_COLUMNS + x;
                    for (int k = s_hash_start[neighbour]; k < s_hash_start[neighbour + 1]; ++k) {
                        int j = s_hash_particle[k];
                        if (j <= i) {
                            continue;
                        }
                        fluid_particle_t *b = &s_particles[j];
                        float dx = (float)(b->x - a->x) / 256.0f;
                        float dy = (float)(b->y - a->y) / 256.0f;
                        float distance2 = dx * dx + dy * dy;
                        if (distance2 >= min_distance2) {
                            continue;
                        }
                        if (distance2 < 0.01f) {
                            dx = (i & 1) ? 0.5f : -0.5f;
                            dy = (i & 2) ? 0.5f : -0.5f;
                            distance2 = dx * dx + dy * dy;
                        }
                        float distance = sqrtf(distance2);
                        float scale = 0.56f * (PARTICLE_MIN_DISTANCE - distance) / distance;
                        int32_t move_x = (int32_t)(dx * scale * 256.0f);
                        int32_t move_y = (int32_t)(dy * scale * 256.0f);
                        a->x -= move_x;
                        a->y -= move_y;
                        b->x += move_x;
                        b->y += move_y;

                        float relative_speed = ((float)(b->vx - a->vx) / 256.0f) * dx / distance +
                                               ((float)(b->vy - a->vy) / 256.0f) * dy / distance;
                        if (relative_speed < 0.0f) {
                            float impulse = -relative_speed * 0.24f;
                            int32_t impulse_x = (int32_t)(impulse * dx * 256.0f / distance);
                            int32_t impulse_y = (int32_t)(impulse * dy * 256.0f / distance);
                            a->vx -= impulse_x;
                            a->vy -= impulse_y;
                            b->vx += impulse_x;
                            b->vy += impulse_y;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < PARTICLE_COUNT; ++i) {
            constrain_to_bowl(&s_particles[i]);
        }
    }
}

static void step_fluid(void)
{
    int gravity_x = 0;
    int gravity_y = 0;
    read_tilt(&gravity_x, &gravity_y);
    poll_touch();

    /* The accelerometer has no X/Y component while the badge lies flat.
     * Give the droplets a gentle downward settle, matching Opal simple mode. */
    if (abs(gravity_x) + abs(gravity_y) < 12) {
        gravity_y = 92;
    }

    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        fluid_particle_t *particle = &s_particles[i];
        int x = (int)(particle->x >> 8);
        int y = (int)(particle->y >> 8);

        particle->vx += gravity_x;
        particle->vy += gravity_y;

        if (s_touch_energy != 0) {
            int delta_x = x - s_touch_x;
            int delta_y = y - s_touch_y;
            int distance2 = delta_x * delta_x + delta_y * delta_y;
            if (distance2 < 34000) {
                particle->vx += (delta_x * s_touch_energy) / 380;
                particle->vy += (delta_y * s_touch_energy) / 380;
            }
        }

        particle->vx = clamp_int((particle->vx * 254) / 256,
                                 -PARTICLE_MAX_SPEED_Q8, PARTICLE_MAX_SPEED_Q8);
        particle->vy = clamp_int((particle->vy * 254) / 256,
                                 -PARTICLE_MAX_SPEED_Q8, PARTICLE_MAX_SPEED_Q8);
        particle->x += particle->vx;
        particle->y += particle->vy;
        constrain_to_bowl(particle);
    }

    separate_particles();
    if (s_touch_energy > 10) {
        s_touch_energy -= 10;
    } else {
        s_touch_energy = 0;
    }
}

/* Superseded by the direct droplet renderer below.  Kept here temporarily as
 * reference while PACON moves from the density-field effect to Opal simple
 * mode; the code is excluded so it cannot allocate or touch display state. */
#if 0
static uint16_t field_at(int x, int y)
{
    x = clamp_int(x, 0, FIELD_WIDTH - 1);
    y = clamp_int(y, 0, FIELD_HEIGHT - 1);
    return s_field[y * FIELD_WIDTH + x];
}

static void build_density_field(void)
{
    const float kernel_radius = 4.15f;
    const float kernel_radius2 = kernel_radius * kernel_radius;
    memset(s_field, 0, sizeof(s_field));
    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        float px = (float)(s_particles[i].x >> 8) * (FIELD_WIDTH - 1) / (LCD_WIDTH - 1);
        float py = (float)(s_particles[i].y >> 8) * (FIELD_HEIGHT - 1) / (LCD_HEIGHT - 1);
        int x0 = clamp_int((int)floorf(px - kernel_radius), 0, FIELD_WIDTH - 1);
        int x1 = clamp_int((int)floorf(px + kernel_radius), 0, FIELD_WIDTH - 1);
        int y0 = clamp_int((int)floorf(py - kernel_radius), 0, FIELD_HEIGHT - 1);
        int y1 = clamp_int((int)floorf(py + kernel_radius), 0, FIELD_HEIGHT - 1);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                float dx = (float)x - px;
                float dy = (float)y - py;
                float kernel = kernel_radius2 - dx * dx - dy * dy;
                if (kernel <= 0.0f) {
                    continue;
                }
                int index = y * FIELD_WIDTH + x;
                int value = s_field[index] + (int)(kernel * 9.5f);
                s_field[index] = clamp_int(value, 0, 255);
            }
        }
    }

    /* The first pass joins neighbouring particle kernels. */
    for (int y = 0; y < FIELD_HEIGHT; ++y) {
        for (int x = 0; x < FIELD_WIDTH; ++x) {
            int center = s_field[y * FIELD_WIDTH + x] * 4;
            int edges = (s_field[y * FIELD_WIDTH + clamp_int(x - 1, 0, FIELD_WIDTH - 1)] +
                         s_field[y * FIELD_WIDTH + clamp_int(x + 1, 0, FIELD_WIDTH - 1)] +
                         s_field[clamp_int(y - 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH + x] +
                         s_field[clamp_int(y + 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH + x]) * 2;
            int corners = s_field[clamp_int(y - 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH +
                                  clamp_int(x - 1, 0, FIELD_WIDTH - 1)] +
                          s_field[clamp_int(y - 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH +
                                  clamp_int(x + 1, 0, FIELD_WIDTH - 1)] +
                          s_field[clamp_int(y + 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH +
                                  clamp_int(x - 1, 0, FIELD_WIDTH - 1)] +
                          s_field[clamp_int(y + 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH +
                                  clamp_int(x + 1, 0, FIELD_WIDTH - 1)];
            s_field_filtered[y * FIELD_WIDTH + x] = (center + edges + corners) / 16;
        }
    }

    /* A second pass is cheap at this field resolution and removes the last
     * square-cell texture before the field is bilinearly enlarged to screen
     * resolution.  s_field is now the final, smoothed surface. */
    for (int y = 0; y < FIELD_HEIGHT; ++y) {
        for (int x = 0; x < FIELD_WIDTH; ++x) {
            int center = s_field_filtered[y * FIELD_WIDTH + x] * 4;
            int edges = (s_field_filtered[y * FIELD_WIDTH + clamp_int(x - 1, 0, FIELD_WIDTH - 1)] +
                         s_field_filtered[y * FIELD_WIDTH + clamp_int(x + 1, 0, FIELD_WIDTH - 1)] +
                         s_field_filtered[clamp_int(y - 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH + x] +
                         s_field_filtered[clamp_int(y + 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH + x]) * 2;
            int corners = s_field_filtered[clamp_int(y - 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH +
                                           clamp_int(x - 1, 0, FIELD_WIDTH - 1)] +
                          s_field_filtered[clamp_int(y - 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH +
                                           clamp_int(x + 1, 0, FIELD_WIDTH - 1)] +
                          s_field_filtered[clamp_int(y + 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH +
                                           clamp_int(x - 1, 0, FIELD_WIDTH - 1)] +
                          s_field_filtered[clamp_int(y + 1, 0, FIELD_HEIGHT - 1) * FIELD_WIDTH +
                                           clamp_int(x + 1, 0, FIELD_WIDTH - 1)];
            s_field[y * FIELD_WIDTH + x] = (center + edges + corners) / 16;
        }
    }

    for (int y = 0; y < FIELD_HEIGHT; ++y) {
        for (int x = 0; x < FIELD_WIDTH; ++x) {
            int index = y * FIELD_WIDTH + x;
            s_field_gradient_x[index] = (int16_t)((int)field_at(x + 1, y) -
                                                   (int)field_at(x - 1, y));
            s_field_gradient_y[index] = (int16_t)((int)field_at(x, y + 1) -
                                                   (int)field_at(x, y - 1));
        }
    }
}

static int sample_density(int x, int y)
{
    int x0 = s_sample_x_cell[x];
    int y0 = s_sample_y_cell[y];
    int x1 = clamp_int(x0 + 1, 0, FIELD_WIDTH - 1);
    int y1 = clamp_int(y0 + 1, 0, FIELD_HEIGHT - 1);
    int tx = s_sample_x_frac[x];
    int ty = s_sample_y_frac[y];
    const uint16_t *top_row = &s_field[y0 * FIELD_WIDTH];
    const uint16_t *bottom_row = &s_field[y1 * FIELD_WIDTH];
    int top = top_row[x0] + ((top_row[x1] - top_row[x0]) * tx >> 8);
    int bottom = bottom_row[x0] + ((bottom_row[x1] - bottom_row[x0]) * tx >> 8);
    return top + ((bottom - top) * ty >> 8);
}

static uint16_t liquid_pixel(int x, int y)
{
    int density = sample_density(x, y);
    int coverage = smoothstep_8(14, 44, density);
    if (coverage == 0) {
        return 0x0000;
    }

    int gx = s_sample_x_cell[x];
    int gy = s_sample_y_cell[y];
    int index = gy * FIELD_WIDTH + gx;
    int fill = smoothstep_8(34, 174, density);
    int slope = s_field_gradient_x[index] * 3 + s_field_gradient_y[index] * 2;
    int directional_light = clamp_int(22 - slope / 5, 0, 58);
    int rim = coverage * (255 - fill) / 255;
    int glint1_x = LCD_WIDTH / 2 + triangle_wave(s_phase * 3U) * 3 / 2;
    int glint1_y = LCD_HEIGHT / 2 - 52 + triangle_wave(s_phase * 2U + 83U) / 2;
    int glint2_x = LCD_WIDTH / 2 - triangle_wave(s_phase * 2U + 171U);
    int glint2_y = LCD_HEIGHT / 2 + 46 + triangle_wave(s_phase * 3U + 39U) / 3;
    int glint = radial_glint(x, y, glint1_x, glint1_y, 72);
    int glint2 = radial_glint(x, y, glint2_x, glint2_y, 44);
    if (glint2 > glint) {
        glint = glint2;
    }

    /* Keep specular light inside the filled portion.  At the soft outer edge
     * it fades with coverage instead of becoming a coloured outline. */
    int specular = glint * fill / 255;
    int light = clamp_int(directional_light + rim * 112 / 255 + specular * 138 / 255,
                          0, 220);

    const liquid_palette_t *palette = active_palette();
    int red = palette->deep[0] + (palette->body[0] - palette->deep[0]) * fill / 255;
    int green = palette->deep[1] + (palette->body[1] - palette->deep[1]) * fill / 255;
    int blue = palette->deep[2] + (palette->body[2] - palette->deep[2]) * fill / 255;
    red += (palette->shine[0] - red) * light / 255;
    green += (palette->shine[1] - green) * light / 255;
    blue += (palette->shine[2] - blue) * light / 255;

    /* RGB565 has no alpha channel; blend the edge into the black background
     * explicitly.  This is what keeps the liquid outline rounded instead of
     * stepping from black straight to a saturated colour. */
    return rgb565(red * coverage / 255, green * coverage / 255,
                  blue * coverage / 255);
}
#endif

static uint16_t droplet_pixel(const fluid_particle_t *particle, int dx, int dy, int distance2)
{
    const liquid_palette_t *palette = active_palette();
    const int inner_radius = PARTICLE_DRAW_RADIUS - 2;
    const int inner_radius2 = inner_radius * inner_radius;

    /* Direct port of Opal simple mode's readable liquid-disc treatment:
     * a coloured core, a low-saturation edge, then only two tiny highlights.
     * The prior radial glint made most of every dot near-white on SH8601. */
    if ((dx == -1 && dy == -2) || (dx == 0 && dy == -2)) {
        return rgb565(palette->shine[0], palette->shine[1], palette->shine[2]);
    }
    if (distance2 <= inner_radius2) {
        int velocity = ((particle->vx < 0 ? -particle->vx : particle->vx) +
                        (particle->vy < 0 ? -particle->vy : particle->vy)) / 180;
        velocity = clamp_int(velocity, 0, 32);
        return rgb565(palette->body[0] + velocity / 4,
                      palette->body[1] + velocity,
                      palette->body[2] + velocity);
    }
    return rgb565(palette->deep[0], palette->deep[1], palette->deep[2]);
}

static bool dirty_rect_valid(const dirty_rect_t *rect)
{
    return rect->x1 < rect->x2 && rect->y1 < rect->y2;
}

static void dirty_rect_include(dirty_rect_t *rect, int x1, int y1, int x2, int y2)
{
    x1 = clamp_int(x1, 0, LCD_WIDTH);
    y1 = clamp_int(y1, 0, LCD_HEIGHT);
    x2 = clamp_int(x2, 0, LCD_WIDTH);
    y2 = clamp_int(y2, 0, LCD_HEIGHT);
    if (x1 >= x2 || y1 >= y2) {
        return;
    }
    if (!dirty_rect_valid(rect)) {
        *rect = (dirty_rect_t){.x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2};
        return;
    }
    rect->x1 = rect->x1 < x1 ? rect->x1 : x1;
    rect->y1 = rect->y1 < y1 ? rect->y1 : y1;
    rect->x2 = rect->x2 > x2 ? rect->x2 : x2;
    rect->y2 = rect->y2 > y2 ? rect->y2 : y2;
}

static void dirty_rect_include_particle(dirty_rect_t *rect, int center_x, int center_y)
{
    const int pad = PARTICLE_DRAW_RADIUS + 3;
    dirty_rect_include(rect, center_x - pad, center_y - pad,
                       center_x + pad + 1, center_y + pad + 1);
}

static void clear_canvas_rect(const dirty_rect_t *rect)
{
    for (int y = rect->y1; y < rect->y2; ++y) {
        memset(s_lcd_canvas + (size_t)y * LCD_WIDTH + rect->x1, 0,
               (size_t)(rect->x2 - rect->x1) * sizeof(uint16_t));
    }
}

static void render_droplet_into_canvas(const fluid_particle_t *particle,
                                       const dirty_rect_t *clip)
{
    const int center_x = (int)(particle->x >> 8);
    const int center_y = (int)(particle->y >> 8);
    const int radius = PARTICLE_DRAW_RADIUS;
    const int radius2 = radius * radius;
    const int clip_x1 = clip == NULL ? 0 : clip->x1;
    const int clip_y1 = clip == NULL ? 0 : clip->y1;
    const int clip_x2 = clip == NULL ? LCD_WIDTH : clip->x2;
    const int clip_y2 = clip == NULL ? LCD_HEIGHT : clip->y2;
    int top = clamp_int(center_y - radius, clip_y1, clip_y2 - 1);
    int bottom = clamp_int(center_y + radius, clip_y1, clip_y2 - 1);

    if (center_x + radius < clip_x1 || center_x - radius >= clip_x2 ||
        center_y + radius < clip_y1 || center_y - radius >= clip_y2) {
        return;
    }

    for (int y = top; y <= bottom; ++y) {
        uint16_t *line = s_lcd_canvas + (size_t)y * LCD_WIDTH;
        int dy = y - center_y;
        int left = clamp_int(center_x - radius, clip_x1, clip_x2 - 1);
        int right = clamp_int(center_x + radius, clip_x1, clip_x2 - 1);
        for (int x = left; x <= right; ++x) {
            int dx = x - center_x;
            int distance2 = dx * dx + dy * dy;
            if (distance2 <= radius2) {
                line[x] = droplet_pixel(particle, dx, dy, distance2);
            }
        }
    }
}

static uint16_t fluid_shape_colour(const fluid_particle_t *particle)
{
    const liquid_palette_t *palette = active_palette();
    int velocity = ((particle->vx < 0 ? -particle->vx : particle->vx) +
                    (particle->vy < 0 ? -particle->vy : particle->vy)) / 160;
    velocity = clamp_int(velocity, 0, 42);
    return rgb565(palette->body[0] + velocity / 5,
                  palette->body[1] + velocity,
                  palette->body[2] + velocity);
}

static void fill_canvas_rect(int x1, int y1, int x2, int y2, uint16_t colour,
                             const dirty_rect_t *clip)
{
    const int clip_x1 = clip == NULL ? 0 : clip->x1;
    const int clip_y1 = clip == NULL ? 0 : clip->y1;
    const int clip_x2 = clip == NULL ? LCD_WIDTH : clip->x2;
    const int clip_y2 = clip == NULL ? LCD_HEIGHT : clip->y2;
    x1 = clamp_int(x1, clip_x1, clip_x2);
    y1 = clamp_int(y1, clip_y1, clip_y2);
    x2 = clamp_int(x2, clip_x1, clip_x2);
    y2 = clamp_int(y2, clip_y1, clip_y2);
    for (int y = y1; y < y2; ++y) {
        uint16_t *line = s_lcd_canvas + (size_t)y * LCD_WIDTH + x1;
        for (int x = x1; x < x2; ++x) {
            *line++ = colour;
        }
    }
}

static void fill_canvas_round_rect(int x1, int y1, int x2, int y2, int radius,
                                   uint16_t colour, const dirty_rect_t *clip)
{
    const int clip_x1 = clip == NULL ? 0 : clip->x1;
    const int clip_y1 = clip == NULL ? 0 : clip->y1;
    const int clip_x2 = clip == NULL ? LCD_WIDTH : clip->x2;
    const int clip_y2 = clip == NULL ? LCD_HEIGHT : clip->y2;
    const int left = clamp_int(x1, clip_x1, clip_x2);
    const int top = clamp_int(y1, clip_y1, clip_y2);
    const int right = clamp_int(x2, clip_x1, clip_x2);
    const int bottom = clamp_int(y2, clip_y1, clip_y2);
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            if (home_in_round_rect(x, y, x1, y1, x2 - 1, y2 - 1, radius)) {
                s_lcd_canvas[(size_t)y * LCD_WIDTH + (size_t)x] = colour;
            }
        }
    }
}

static void render_blocks_into_canvas(const dirty_rect_t *clip)
{
    for (int index = 0; index < PARTICLE_COUNT; ++index) {
        const fluid_particle_t *particle = &s_particles[index];
        int center_x = (int)(particle->x >> 8);
        int center_y = (int)(particle->y >> 8);
        uint16_t body = fluid_shape_colour(particle);
        fill_canvas_rect(center_x - 6, center_y - 6, center_x + 7, center_y + 7,
                         body, clip);
        fill_canvas_rect(center_x - 3, center_y - 4, center_x + 2, center_y + 1,
                         rgb565_blend(body, rgb565(215, 250, 255), 115), clip);
    }
}

static bool matrix_find_free_cell(int *column, int *row)
{
    for (int radius = 0; radius < MATRIX_COLUMNS + MATRIX_ROWS; ++radius) {
        for (int y = *row - radius; y <= *row + radius; ++y) {
            if (y < 0 || y >= MATRIX_ROWS) {
                continue;
            }
            for (int x = *column - radius; x <= *column + radius; ++x) {
                if (x < 0 || x >= MATRIX_COLUMNS ||
                    (radius != 0 && x > *column - radius && x < *column + radius &&
                     y > *row - radius && y < *row + radius)) {
                    continue;
                }
                int cell = y * MATRIX_COLUMNS + x;
                if (!s_matrix_cell_used[cell]) {
                    *column = x;
                    *row = y;
                    s_matrix_cell_used[cell] = true;
                    return true;
                }
            }
        }
    }
    return false;
}

static void prepare_matrix_layout(void)
{
    memset(s_matrix_cell_used, 0, sizeof(s_matrix_cell_used));
    for (int index = 0; index < PARTICLE_COUNT; ++index) {
        const fluid_particle_t *particle = &s_particles[index];
        int column = clamp_int((int)(particle->x >> 8) / MATRIX_CELL_SIZE,
                               0, MATRIX_COLUMNS - 1);
        int row = clamp_int((int)(particle->y >> 8) / MATRIX_CELL_SIZE,
                            0, MATRIX_ROWS - 1);
        if (!matrix_find_free_cell(&column, &row)) {
            s_matrix_x[index] = -1;
            s_matrix_y[index] = -1;
            continue;
        }
        s_matrix_x[index] = (int16_t)(column * MATRIX_CELL_SIZE);
        s_matrix_y[index] = (int16_t)(row * MATRIX_CELL_SIZE);
    }
    s_matrix_layout_ready = true;
}

static void render_matrix_into_canvas(const dirty_rect_t *clip)
{
    const int x1 = clip == NULL ? 0 : clip->x1;
    const int y1 = clip == NULL ? 0 : clip->y1;
    const int x2 = clip == NULL ? LCD_WIDTH : clip->x2;
    const int y2 = clip == NULL ? LCD_HEIGHT : clip->y2;
    const uint16_t grid = rgb565(4, 21, 34);
    for (int y = y1; y < y2; ++y) {
        uint16_t *line = s_lcd_canvas + (size_t)y * LCD_WIDTH;
        for (int x = x1; x < x2; ++x) {
            if ((x % MATRIX_CELL_SIZE) == 0 || (y % MATRIX_CELL_SIZE) == 0) {
                line[x] = grid;
            }
        }
    }

    if (!s_matrix_layout_ready) {
        prepare_matrix_layout();
    }
    for (int index = 0; index < PARTICLE_COUNT; ++index) {
        const fluid_particle_t *particle = &s_particles[index];
        if (s_matrix_x[index] < 0 || s_matrix_y[index] < 0) {
            continue;
        }
        int left = s_matrix_x[index] + 3;
        int top = s_matrix_y[index] + 3;
        uint16_t body = fluid_shape_colour(particle);
        fill_canvas_rect(left, top, left + MATRIX_CELL_SIZE - 5,
                         top + MATRIX_CELL_SIZE - 5, body, clip);
        fill_canvas_rect(left + 2, top + 2, left + 5, top + 5,
                         rgb565_blend(body, rgb565(220, 251, 255), 130), clip);
    }
}

static void render_fluid_shape_into_canvas(const dirty_rect_t *clip)
{
    if (s_fluid_shape == FLUID_SHAPE_BLOCKS) {
        render_blocks_into_canvas(clip);
        return;
    }
    if (s_fluid_shape == FLUID_SHAPE_MATRIX) {
        render_matrix_into_canvas(clip);
        return;
    }
    for (int particle = 0; particle < PARTICLE_COUNT; ++particle) {
        render_droplet_into_canvas(&s_particles[particle], clip);
    }
}


static void sync_drawn_particle_positions(void)
{
    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        if (s_fluid_shape == FLUID_SHAPE_MATRIX &&
            s_matrix_x[i] >= 0 && s_matrix_y[i] >= 0) {
            s_drawn_x[i] = s_matrix_x[i] + MATRIX_CELL_SIZE / 2;
            s_drawn_y[i] = s_matrix_y[i] + MATRIX_CELL_SIZE / 2;
        } else {
            s_drawn_x[i] = (int16_t)(s_particles[i].x >> 8);
            s_drawn_y[i] = (int16_t)(s_particles[i].y >> 8);
        }
    }
}

static bool flush_canvas_rect(const dirty_rect_t *rect)
{
    /* Keep two stripe buffers in flight.  The previous settings path waited
     * for every stripe before composing the next one, so a vertical drag was
     * serialized into a long sequence of small QSPI transfers. */
    uint16_t *const stripe_buffers[2] = {s_lcd_stripe, s_lcd_stripe_secondary};
    uint8_t queued = 0;
    uint8_t stripe_index = 0;
    for (int stripe_y = rect->y1; stripe_y < rect->y2; stripe_y += LCD_STRIPE_LINES) {
        if (queued == 2) {
            if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
                ESP_LOGE(TAG, "LCD dirty DMA completion timed out");
                return false;
            }
            --queued;
        }
        int stripe_end = clamp_int(stripe_y + LCD_STRIPE_LINES, stripe_y + 1, rect->y2);
        const int width = rect->x2 - rect->x1;
        uint16_t *stripe = stripe_buffers[stripe_index & 1U];
        for (int y = stripe_y; y < stripe_end; ++y) {
            const uint16_t *source = s_lcd_canvas + (size_t)y * LCD_WIDTH + rect->x1;
            uint16_t *target = stripe + (size_t)(y - stripe_y) * width;
            for (int x = 0; x < width; ++x) {
                target[x] = rgb565_for_sh8601(source[x]);
            }
        }

        esp_err_t err = esp_lcd_panel_draw_bitmap(s_lcd_panel, rect->x1, stripe_y,
                                                   rect->x2, stripe_end, stripe);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LCD dirty transfer failed: %s", esp_err_to_name(err));
            return false;
        }
        ++queued;
        ++stripe_index;
    }
    while (queued > 0) {
        if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
            ESP_LOGE(TAG, "LCD dirty DMA completion timed out");
            return false;
        }
        --queued;
    }
    return true;
}

/* --------------------------------------------------------------------------
 * SkyOrb-compatible Plane Radar
 *
 * MatixYo/ESP32-Plane-Radar is the implementation baseline: it is MIT
 * licensed and has a useful AP setup + nearby-aircraft radar design.  This is
 * a native ESP-IDF port for PACON's single, larger QSPI panel.  It purposefully
 * does not pull in Arduino, PlatformIO or a second display driver.
 * -------------------------------------------------------------------------- */

/* Keep the physical scale identical to the values exposed by the UI/BLE API. */
/* Clockwise rotation advances through six useful city-scale radar ranges.
 * Keep the index stable in NVS/BLE; the Android client mirrors this table. */
static const float s_skyorb_ranges_km[] = {
    5.0f, 10.0f, 15.0f, 25.0f, 35.0f, 50.0f
};
#define SKYORB_RANGE_COUNT \
    ((uint8_t)(sizeof(s_skyorb_ranges_km) / sizeof(s_skyorb_ranges_km[0])))

static void skyorb_mark_dirty(void)
{
    s_skyorb_dirty = true;
    s_device_settings_dirty = true;
    s_wifi_settings_dirty = true;
}

static bool skyorb_nvs_open(nvs_handle_t *handle, nvs_open_mode_t mode)
{
    esp_err_t err = nvs_open(SKYORB_CONFIG_NAMESPACE, mode, handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SkyOrb: NVS open failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static void wifi_config_select_profile(skyorb_config_t *config, uint8_t index)
{
    if (config == NULL || config->wifi_count == 0 || index >= config->wifi_count) {
        if (config != NULL) {
            config->wifi_selected = 0;
            config->wifi_valid = false;
            config->ssid[0] = '\0';
            config->password[0] = '\0';
        }
        return;
    }
    char selected_ssid[sizeof(config->ssid)];
    char selected_password[sizeof(config->password)];
    snprintf(selected_ssid, sizeof(selected_ssid), "%s", config->wifi_profiles[index].ssid);
    snprintf(selected_password, sizeof(selected_password), "%s",
             config->wifi_profiles[index].password);
    config->wifi_selected = index;
    memcpy(config->ssid, selected_ssid, sizeof(config->ssid));
    memcpy(config->password, selected_password, sizeof(config->password));
    config->wifi_valid = config->ssid[0] != '\0';
}

static int wifi_config_find_profile(const skyorb_config_t *config, const char *ssid)
{
    if (config == NULL || ssid == NULL) return -1;
    for (uint8_t index = 0; index < config->wifi_count; ++index) {
        if (strcmp(config->wifi_profiles[index].ssid, ssid) == 0) return index;
    }
    return -1;
}

static bool wifi_config_upsert_profile(skyorb_config_t *config, const char *ssid,
                                       const char *password)
{
    if (config == NULL || ssid == NULL || password == NULL || ssid[0] == '\0') return false;
    int index = wifi_config_find_profile(config, ssid);
    if (index < 0) {
        if (config->wifi_count >= WIFI_PROFILE_MAX) return false;
        index = config->wifi_count++;
    }
    snprintf(config->wifi_profiles[index].ssid, sizeof(config->wifi_profiles[index].ssid),
             "%s", ssid);
    snprintf(config->wifi_profiles[index].password,
             sizeof(config->wifi_profiles[index].password), "%s", password);
    wifi_config_select_profile(config, (uint8_t)index);
    return true;
}

static bool wifi_config_delete_profile(skyorb_config_t *config, uint8_t index)
{
    if (config == NULL || index >= config->wifi_count) return false;
    for (uint8_t move = index; move + 1U < config->wifi_count; ++move) {
        config->wifi_profiles[move] = config->wifi_profiles[move + 1U];
    }
    if (config->wifi_count > 0) {
        --config->wifi_count;
        memset(&config->wifi_profiles[config->wifi_count], 0,
               sizeof(config->wifi_profiles[config->wifi_count]));
    }
    uint8_t selected = config->wifi_selected;
    if (config->wifi_count == 0) {
        selected = 0;
    } else if (selected > index) {
        --selected;
    } else if (selected >= config->wifi_count) {
        selected = config->wifi_count - 1U;
    }
    wifi_config_select_profile(config, selected);
    return true;
}

static void skyorb_load_config(void)
{
    skyorb_config_t config = {0};
    config.range_index = 1;
    nvs_handle_t handle;
    if (skyorb_nvs_open(&handle, NVS_READONLY)) {
        char legacy_ssid[sizeof(config.ssid)] = {0};
        char legacy_password[sizeof(config.password)] = {0};
        size_t ssid_length = sizeof(legacy_ssid);
        size_t password_length = sizeof(legacy_password);
        int32_t latitude_e6 = 0;
        int32_t longitude_e6 = 0;
        uint8_t range = 1;
        uint8_t location_auto = 0;
        const bool have_ssid = nvs_get_str(handle, "ssid", legacy_ssid, &ssid_length) == ESP_OK;
        const bool have_password = nvs_get_str(handle, "password", legacy_password,
                                                &password_length) == ESP_OK;
        uint8_t stored_count = 0;
        uint8_t stored_selected = 0;
        const bool have_profile_schema = nvs_get_u8(handle, "wifi_count", &stored_count) == ESP_OK;
        (void)nvs_get_u8(handle, "wifi_sel", &stored_selected);
        stored_count = stored_count > WIFI_PROFILE_MAX ? WIFI_PROFILE_MAX : stored_count;
        if (have_profile_schema) {
            for (uint8_t index = 0; index < stored_count; ++index) {
                char ssid_key[8];
                char pass_key[8];
                snprintf(ssid_key, sizeof(ssid_key), "ssid%u", (unsigned)index);
                snprintf(pass_key, sizeof(pass_key), "pass%u", (unsigned)index);
                wifi_profile_t profile = {0};
                size_t profile_ssid_length = sizeof(profile.ssid);
                size_t profile_password_length = sizeof(profile.password);
                const bool profile_ok =
                    nvs_get_str(handle, ssid_key, profile.ssid, &profile_ssid_length) == ESP_OK &&
                    nvs_get_str(handle, pass_key, profile.password, &profile_password_length) == ESP_OK &&
                    profile.ssid[0] != '\0';
                if (profile_ok) config.wifi_profiles[config.wifi_count++] = profile;
            }
        }
        if (config.wifi_count == 0 && have_ssid && have_password && legacy_ssid[0] != '\0') {
            snprintf(config.wifi_profiles[0].ssid, sizeof(config.wifi_profiles[0].ssid),
                     "%s", legacy_ssid);
            snprintf(config.wifi_profiles[0].password,
                     sizeof(config.wifi_profiles[0].password), "%s", legacy_password);
            config.wifi_count = 1;
            stored_selected = 0;
            ESP_LOGI(TAG, "Wi-Fi: migrated legacy saved network into profile list");
        }
        const bool have_lat = nvs_get_i32(handle, "lat_e6", &latitude_e6) == ESP_OK;
        const bool have_lon = nvs_get_i32(handle, "lon_e6", &longitude_e6) == ESP_OK;
        (void)nvs_get_u8(handle, "range", &range);
        (void)nvs_get_u8(handle, "loc_auto", &location_auto);
        nvs_close(handle);
        config.latitude = (float)latitude_e6 / 1000000.0f;
        config.longitude = (float)longitude_e6 / 1000000.0f;
        config.range_index = range < SKYORB_RANGE_COUNT ? range : 1;
        wifi_config_select_profile(&config,
                                   stored_selected < config.wifi_count ? stored_selected : 0);
        config.location_valid = have_lat && have_lon && fabsf(config.latitude) <= 90.0f &&
                                fabsf(config.longitude) <= 180.0f;
        config.location_auto = location_auto != 0;
    }
    if (s_skyorb_mutex != NULL) {
        xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
        s_skyorb_config = config;
        s_skyorb_range_index = config.range_index;
        xSemaphoreGive(s_skyorb_mutex);
    } else {
        s_skyorb_config = config;
        s_skyorb_range_index = config.range_index;
    }
    s_skyorb_config_loaded = true;
    ESP_LOGI(TAG, "SkyOrb: Wi-Fi=%u saved selected=%u location=%s range=%u",
             (unsigned)config.wifi_count, (unsigned)config.wifi_selected,
             config.location_valid ? (config.location_auto ? "automatic" : "manual") : "none",
             (unsigned)config.range_index);
    skyorb_mark_dirty();
}

static void skyorb_save_config(const skyorb_config_t *config)
{
    nvs_handle_t handle;
    if (!skyorb_nvs_open(&handle, NVS_READWRITE)) {
        return;
    }
    esp_err_t err = ESP_OK;
    if (config->wifi_valid) {
        err = nvs_set_str(handle, "ssid", config->ssid);
        if (err == ESP_OK) err = nvs_set_str(handle, "password", config->password);
    } else {
        (void)nvs_erase_key(handle, "ssid");
        (void)nvs_erase_key(handle, "password");
    }
    if (err == ESP_OK) err = nvs_set_u8(handle, "wifi_count", config->wifi_count);
    if (err == ESP_OK) err = nvs_set_u8(handle, "wifi_sel", config->wifi_selected);
    for (uint8_t index = 0; err == ESP_OK && index < WIFI_PROFILE_MAX; ++index) {
        char ssid_key[8];
        char pass_key[8];
        snprintf(ssid_key, sizeof(ssid_key), "ssid%u", (unsigned)index);
        snprintf(pass_key, sizeof(pass_key), "pass%u", (unsigned)index);
        if (index < config->wifi_count) {
            err = nvs_set_str(handle, ssid_key, config->wifi_profiles[index].ssid);
            if (err == ESP_OK) {
                err = nvs_set_str(handle, pass_key, config->wifi_profiles[index].password);
            }
        } else {
            (void)nvs_erase_key(handle, ssid_key);
            (void)nvs_erase_key(handle, pass_key);
        }
    }
    if (err == ESP_OK && config->location_valid) {
        const int32_t latitude_e6 = (int32_t)lroundf(config->latitude * 1000000.0f);
        const int32_t longitude_e6 = (int32_t)lroundf(config->longitude * 1000000.0f);
        err = nvs_set_i32(handle, "lat_e6", latitude_e6);
        if (err == ESP_OK) err = nvs_set_i32(handle, "lon_e6", longitude_e6);
        if (err == ESP_OK) err = nvs_set_u8(handle, "loc_auto", config->location_auto ? 1 : 0);
    } else if (err == ESP_OK) {
        (void)nvs_erase_key(handle, "lat_e6");
        (void)nvs_erase_key(handle, "lon_e6");
        (void)nvs_erase_key(handle, "loc_auto");
    }
    if (err == ESP_OK) err = nvs_set_u8(handle, "range", config->range_index);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SkyOrb: NVS save failed: %s", esp_err_to_name(err));
    }
}

static void skyorb_save_range(void)
{
    nvs_handle_t handle;
    if (!skyorb_nvs_open(&handle, NVS_READWRITE)) {
        return;
    }
    esp_err_t err = nvs_set_u8(handle, "range", s_skyorb_range_index);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SkyOrb: range save failed: %s", esp_err_to_name(err));
    }
}

static void ble_trim_ascii(char *text)
{
    if (text == NULL) return;
    char *first = text;
    while (*first != '\0' && isspace((unsigned char)*first)) ++first;
    if (first != text) memmove(text, first, strlen(first) + 1U);
    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1U])) {
        text[--length] = '\0';
    }
}

static void ble_json_escape(char *destination, size_t destination_size, const char *source)
{
    if (destination == NULL || destination_size == 0) return;
    size_t written = 0;
    if (source != NULL) {
        while (*source != '\0' && written + 1U < destination_size) {
            const unsigned char value = (unsigned char)*source++;
            if ((value == '"' || value == '\\') && written + 2U < destination_size) {
                destination[written++] = '\\';
                destination[written++] = (char)value;
            } else if (value >= 0x20U) {
                destination[written++] = (char)value;
            }
        }
    }
    destination[written] = '\0';
}

static void ble_snapshot_skyorb_config(skyorb_config_t *config)
{
    if (config == NULL) return;
    if (!s_skyorb_config_loaded) skyorb_load_config();
    if (s_skyorb_mutex != NULL) {
        xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
        *config = s_skyorb_config;
        xSemaphoreGive(s_skyorb_mutex);
    } else {
        *config = s_skyorb_config;
    }
}

static void ble_store_skyorb_config(const skyorb_config_t *config)
{
    if (config == NULL) return;
    if (s_skyorb_mutex != NULL) {
        xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
        s_skyorb_config = *config;
        s_skyorb_range_index = config->range_index;
        xSemaphoreGive(s_skyorb_mutex);
    } else {
        s_skyorb_config = *config;
        s_skyorb_range_index = config->range_index;
    }
    s_skyorb_config_loaded = true;
    skyorb_save_config(config);
    skyorb_mark_dirty();
}

static const char *skyorb_location_error_name(skyorb_location_result_t result)
{
    switch (result) {
        case SKYORB_LOCATION_ERR_CLIENT: return "client_init";
        case SKYORB_LOCATION_ERR_OPEN: return "network_open";
        case SKYORB_LOCATION_ERR_RESPONSE: return "network_read";
        case SKYORB_LOCATION_ERR_HTTP: return "http_status";
        case SKYORB_LOCATION_ERR_PARSE: return "invalid_response";
        default: return "none";
    }
}

static const char *skyorb_location_state_name(const skyorb_config_t *config)
{
    if (config->location_valid) return "ready";
    if (s_skyorb_location_in_progress ||
        s_skyorb_location_result == SKYORB_LOCATION_IN_PROGRESS) return "pending";
    if (!s_skyorb_wifi_enabled || !s_skyorb_wifi_connected) return "waiting_wifi";
    if (s_skyorb_location_result >= SKYORB_LOCATION_ERR_CLIENT) return "failed";
    if (config->location_auto ||
        s_skyorb_location_result == SKYORB_LOCATION_REQUESTED) return "requested";
    return "unset";
}

/* BLE configuration protocol.  Commands are ASCII and intentionally short so
 * they can be entered directly in nRF Connect:
 *   GET STATUS / GET SETTINGS
 *   SET BRIGHTNESS <0..100>
 *   SET RANGE <0..5>  (5/10/15/25/35/50 km)
 *   SET LOCATION <latitude> <longitude>
 *   SET AUTO_LOCATION
 *   SET WIFI <ssid>|<password>
 *   CAMERA SHUTTER / SET CAMERA REMOTE ON|OFF
 */
static esp_err_t fluid_ble_command(const char *command, char *response,
                                   size_t response_size)
{
    if (command == NULL || response == NULL || response_size < 8U) {
        return ESP_ERR_INVALID_ARG;
    }
    response[0] = '\0';
    snprintf(s_ble_command_text, sizeof(s_ble_command_text), "%s", command);
    char *text = s_ble_command_text;
    ble_trim_ascii(text);
    if (text[0] == '\0') {
        snprintf(response, response_size, "ERR empty command\r\n");
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t media_command_result = fluid_ble_media_command(text, response,
                                                                    response_size);
    if (media_command_result != ESP_ERR_NOT_FOUND) {
        return media_command_result;
    }

    if (strcasecmp(text, "GET UI") == 0) {
        portENTER_CRITICAL(&s_ble_ui_lock);
        const ui_screen_t screen = s_ble_ui_snapshot;
        const ble_ui_state_t state = s_ble_ui_state;
        portEXIT_CRITICAL(&s_ble_ui_lock);
        const char *state_name = state == BLE_UI_PENDING ? "pending" :
                                 state == BLE_UI_DONE ? "done" :
                                 state == BLE_UI_BUSY ? "busy" : "idle";
        snprintf(response, response_size,
                 "{\"ok\":true,\"screen\":\"%s\",\"state\":\"%s\"}\r\n",
                 ble_ui_screen_name(screen), state_name);
        return ESP_OK;
    }
    if (strncasecmp(text, "SET UI ", 7U) == 0) {
        ui_screen_t target;
        if (strcasecmp(text + 7, "HOME") == 0) target = UI_SCREEN_HOME;
        else if (strcasecmp(text + 7, "FLUID") == 0) target = UI_SCREEN_FLUID;
        else if (strcasecmp(text + 7, "OUO") == 0) target = UI_SCREEN_OUO;
        else {
            snprintf(response, response_size, "ERR UI expects HOME|FLUID|OUO\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        portENTER_CRITICAL(&s_ble_ui_lock);
        bool pending = s_ble_ui_state == BLE_UI_PENDING;
        if (!pending) s_ble_ui_state = BLE_UI_PENDING;
        portEXIT_CRITICAL(&s_ble_ui_lock);
        if (pending) {
            snprintf(response, response_size, "ERR UI busy\r\n");
            return ESP_ERR_INVALID_STATE;
        }
        if (s_ble_ui_requests == NULL || s_ble_media_transfer_active || s_usb_msc_started ||
            s_usb_msc_start_requested || xQueueSend(s_ble_ui_requests, &target, 0) != pdTRUE) {
            portENTER_CRITICAL(&s_ble_ui_lock);
            s_ble_ui_state = BLE_UI_BUSY;
            portEXIT_CRITICAL(&s_ble_ui_lock);
            snprintf(response, response_size, "ERR UI busy\r\n");
            return ESP_ERR_INVALID_STATE;
        }
        snprintf(response, response_size, "OK UI QUEUED %s\r\n", ble_ui_screen_name(target));
        return ESP_OK;
    }

    skyorb_config_t config;
    ble_snapshot_skyorb_config(&config);
    if (strcasecmp(text, "GET RADAR") == 0) {
        snprintf(response, response_size, "{\"ok\":true,\"location_valid\":%s,\"location_auto\":%s,\"location_state\":\"%s\",\"location_error\":\"%s\",\"lat\":%.6f,\"lon\":%.6f,\"range\":%u}\r\n",
                 config.location_valid ? "true" : "false",
                 config.location_auto ? "true" : "false",
                 skyorb_location_state_name(&config),
                 skyorb_location_error_name(s_skyorb_location_result),
                 (double)config.latitude, (double)config.longitude,
                 (unsigned)config.range_index);
        return ESP_OK;
    }
    if (strcasecmp(text, "GET CLOCK") == 0) {
        const clock_time_t time = s_clock_cached_time;
        snprintf(response, response_size,
                 "{\"ok\":true,\"time\":\"%04d-%02d-%02d %02d:%02d:%02d\","
                 "\"rtc_valid\":%s,\"source\":\"%s\",\"sync\":\"%s\","
                 "\"alarm_enabled\":%s,\"alarm\":\"%02u:%02u\","
                 "\"ringing\":%s,\"style\":%u,\"style_name\":\"%s\"}\r\n",
                 time.year, time.month, time.day, time.hour, time.minute, time.second,
                 time.valid ? "true" : "false", clock_source_name(s_clock_source),
                 clock_sync_name(s_clock_sync_state), s_alarm_enabled ? "true" : "false",
                 s_alarm_hour, s_alarm_minute, s_alarm_ringing ? "true" : "false",
                 (unsigned)s_watch_style, s_watch_style == 1U ? "KKD2" : "KKD1");
        return ESP_OK;
    }
    if (strcasecmp(text, "GET SETTINGS") == 0) {
        snprintf(response, response_size,
                 "{\"ok\":true,\"brightness\":%u,\"screen_timeout\":%u,\"camera_remote\":%s}\r\n",
                 (unsigned)(((uint16_t)(s_user_brightness - SETTINGS_BRIGHTNESS_MIN) * 100U) /
                            (SETTINGS_BRIGHTNESS_MAX - SETTINGS_BRIGHTNESS_MIN)),
                 (unsigned)s_display_sleep_timeout_seconds,
                 ble_pacon_is_camera_remote_enabled() ? "true" : "false");
        return ESP_OK;
    }
    if (strcasecmp(text, "GET STATUS") == 0) {
        /* Keep this below a single ATT notification at the negotiated
         * 256-byte MTU. Radar and Wi-Fi details have dedicated commands. */
        snprintf(response, response_size,
                 "{\"ok\":true,\"battery_mv\":%u,\"battery_pct\":%u,"
                 "\"battery_valid\":%s,\"charging\":%s,\"vbus\":%s,"
                 "\"brightness\":%u,\"screen_timeout\":%u,\"ble\":%s,\"wifi\":%s,"
                 "\"camera_remote\":%s,\"media\":%u,\"media_index\":%u}\r\n",
                 (unsigned)s_battery_mv, (unsigned)s_battery_percent,
                 s_battery_percent_valid ? "true" : "false",
                 s_battery_charging ? "true" : "false",
                 s_vbus_present ? "true" : "false",
                 (unsigned)(((uint16_t)(s_user_brightness - SETTINGS_BRIGHTNESS_MIN) * 100U) /
                            (SETTINGS_BRIGHTNESS_MAX - SETTINGS_BRIGHTNESS_MIN)),
                 (unsigned)s_display_sleep_timeout_seconds,
                 ble_pacon_is_enabled() ? "true" : "false",
                 config.wifi_valid ? "true" : "false",
                 ble_pacon_is_camera_remote_enabled() ? "true" : "false",
                 (unsigned)s_home_external_media_count,
                 (unsigned)s_home_media_index);
        return ESP_OK;
    }
    if (strcasecmp(text, "CAMERA SHUTTER") == 0) {
        const esp_err_t shutter_err = ble_pacon_camera_shutter();
        if (shutter_err != ESP_OK) {
            snprintf(response, response_size, "ERR camera remote unavailable\r\n");
            return shutter_err;
        }
        snprintf(response, response_size, "OK CAMERA SHUTTER\r\n");
        return ESP_OK;
    }
    if (strncasecmp(text, "SET CAMERA REMOTE ", 19U) == 0) {
        const char *value = text + 19;
        if (strcasecmp(value, "ON") != 0 && strcasecmp(value, "OFF") != 0) {
            snprintf(response, response_size, "ERR camera remote expects ON|OFF\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        const bool enabled = strcasecmp(value, "ON") == 0;
        (void)ble_pacon_set_camera_remote_enabled(enabled);
        snprintf(response, response_size, "OK CAMERA REMOTE %s\r\n",
                 enabled ? "ON" : "OFF");
        return ESP_OK;
    }
    if (strcasecmp(text, "GET WIFI LIST") == 0) {
        snprintf(response, response_size,
                 "{\"ok\":true,\"count\":%u,\"selected\":%u}\r\n",
                 (unsigned)config.wifi_count, (unsigned)config.wifi_selected);
        return ESP_OK;
    }
    if (strncasecmp(text, "GET WIFI ", 9U) == 0) {
        char *end = NULL;
        const long index = strtol(text + 9, &end, 10);
        while (end != NULL && *end != '\0' && isspace((unsigned char)*end)) ++end;
        if (end == NULL || *end != '\0' || index < 0 ||
            index >= config.wifi_count) {
            snprintf(response, response_size, "ERR invalid Wi-Fi index\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        char escaped_ssid[68];
        ble_json_escape(escaped_ssid, sizeof(escaped_ssid),
                        config.wifi_profiles[index].ssid);
        snprintf(response, response_size,
                 "{\"ok\":true,\"index\":%ld,\"ssid\":\"%s\",\"selected\":%s}\r\n",
                 index, escaped_ssid, index == config.wifi_selected ? "true" : "false");
        return ESP_OK;
    }
    if (strcasecmp(text, "GET HELP") == 0) {
        snprintf(response, response_size,
                 "GET UI; SET UI HOME|FLUID|OUO; GET STATUS; GET RADAR; GET CLOCK; SET BRIGHTNESS 0..100; "
                 "SET SCREEN TIMEOUT 0|15|30|60|120|300; SET RANGE 0..5; "
                 "CAMERA SHUTTER; SET CAMERA REMOTE ON|OFF; "
                 "SET LOCATION lat lon; SET AUTO_LOCATION; SET WIFI ssid|password; "
                 "GET WIFI LIST; GET WIFI index; SELECT WIFI index; DELETE WIFI index; "
                 "SET TIME yyyy-mm-dd hh:mm:ss CUSTOM|BLE; SYNC WIFI TIME; "
                 "SET ALARM hh:mm; SET ALARM OFF; TEST ALARM; STOP ALARM; SET WATCH STYLE 0|1; "
                 "MEDIA_LIST; MEDIA_INFO index; MEDIA_PLAY name; MEDIA_DELETE name; "
                 "MEDIA_BEGIN name size frames fps; MEDIA_DATA offset hex; MEDIA_END\r\n");
        return ESP_OK;
    }
    if (strncasecmp(text, "SET TIME ", 9U) == 0) {
        clock_time_t time = {0};
        char source[10] = {0};
        if (sscanf(text + 9, "%d-%d-%d %d:%d:%d %9s", &time.year, &time.month,
                   &time.day, &time.hour, &time.minute, &time.second, source) != 7) {
            snprintf(response, response_size, "ERR time format yyyy-mm-dd hh:mm:ss CUSTOM|BLE\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        time.valid = clock_time_valid(&time);
        if (!time.valid || (strcasecmp(source, "CUSTOM") != 0 &&
                            strcasecmp(source, "BLE") != 0)) {
            snprintf(response, response_size, "ERR invalid time or source\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        s_clock_pending_time = time;
        s_clock_pending_source = strcasecmp(source, "BLE") == 0 ?
                                 CLOCK_SOURCE_BLE : CLOCK_SOURCE_CUSTOM;
        s_clock_time_pending = true;
        snprintf(response, response_size, "OK TIME PENDING\r\n");
        return ESP_OK;
    }
    if (strcasecmp(text, "SYNC WIFI TIME") == 0) {
        s_clock_wifi_sync_requested = true;
        s_clock_sync_state = s_skyorb_wifi_connected ? CLOCK_SYNC_IDLE : CLOCK_SYNC_WAIT_WIFI;
        snprintf(response, response_size, "OK WIFI TIME PENDING\r\n");
        return ESP_OK;
    }
    if (strcasecmp(text, "SET ALARM OFF") == 0) {
        s_alarm_enabled = false;
        alarm_stop();
        clock_save_preferences();
        snprintf(response, response_size, "OK ALARM OFF\r\n");
        return ESP_OK;
    }
    if (strncasecmp(text, "SET ALARM ", 10U) == 0) {
        int hour = -1;
        int minute = -1;
        char extra = '\0';
        if (sscanf(text + 10, "%d:%d %c", &hour, &minute, &extra) != 2 ||
            hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            snprintf(response, response_size, "ERR alarm format hh:mm\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        s_alarm_hour = (uint8_t)hour;
        s_alarm_minute = (uint8_t)minute;
        s_alarm_enabled = true;
        s_alarm_last_date_key = -1;
        clock_save_preferences();
        s_watch_dirty = true;
        snprintf(response, response_size, "OK ALARM %02d:%02d\r\n", hour, minute);
        return ESP_OK;
    }
    if (strcasecmp(text, "STOP ALARM") == 0) {
        alarm_stop();
        snprintf(response, response_size, "OK ALARM STOPPED\r\n");
        return ESP_OK;
    }
    if (strcasecmp(text, "TEST ALARM") == 0) {
        alarm_start(xTaskGetTickCount());
        if (!s_alarm_ringing) {
            snprintf(response, response_size, "ERR ALARM BUZZER START\r\n");
            return ESP_FAIL;
        }
        snprintf(response, response_size, "OK ALARM TEST\r\n");
        return ESP_OK;
    }
    if (strncasecmp(text, "SET WATCH STYLE ", 16U) == 0) {
        char *end = NULL;
        const long style = strtol(text + 16, &end, 10);
        while (end != NULL && *end != '\0' && isspace((unsigned char)*end)) ++end;
        if (end == NULL || *end != '\0' || style < 0 || style > 1) {
            snprintf(response, response_size, "ERR watch style must be 0..1\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        s_watch_style = (uint8_t)style;
        clock_save_preferences();
        s_watch_dirty = true;
        s_watch_last_frame = 0;
        s_watch_display_seconds = -1;
        ESP_LOGI(TAG, "Watch style requested over BLE: %s (%ld)",
                 style == 1 ? "KKD2" : "KKD1", style);
        snprintf(response, response_size, "OK WATCH STYLE %ld %s\r\n",
                 style, style == 1 ? "KKD2" : "KKD1");
        return ESP_OK;
    }
    if (strncasecmp(text, "SET BRIGHTNESS ", 15U) == 0) {
        char *end = NULL;
        const long percent = strtol(text + 15, &end, 10);
        while (end != NULL && *end != '\0' && isspace((unsigned char)*end)) ++end;
        if (end == NULL || *end != '\0' || percent < 0 || percent > 100) {
            snprintf(response, response_size, "ERR brightness must be 0..100\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        const uint8_t brightness = (uint8_t)(SETTINGS_BRIGHTNESS_MIN +
            ((uint32_t)percent * (SETTINGS_BRIGHTNESS_MAX - SETTINGS_BRIGHTNESS_MIN)) / 100U);
        s_ble_pending_brightness = brightness;
        s_ble_brightness_pending = true;
        snprintf(response, response_size, "OK BRIGHTNESS %ld\r\n", percent);
        return ESP_OK;
    }
    if (strncasecmp(text, "SET SCREEN TIMEOUT ", 19U) == 0) {
        char *end = NULL;
        const long seconds = strtol(text + 19, &end, 10);
        while (end != NULL && *end != '\0' && isspace((unsigned char)*end)) ++end;
        const bool valid = seconds == 0 || seconds == 15 || seconds == 30 ||
                           seconds == 60 || seconds == 120 || seconds == 300;
        if (end == NULL || *end != '\0' || !valid) {
            snprintf(response, response_size,
                     "ERR screen timeout must be 0|15|30|60|120|300\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        s_display_sleep_timeout_seconds = (uint16_t)seconds;
        settings_save_display_timeout();
        s_last_user_activity = xTaskGetTickCount();
        snprintf(response, response_size, "OK SCREEN TIMEOUT %ld\r\n", seconds);
        return ESP_OK;
    }
    if (strncasecmp(text, "SET RANGE ", 10U) == 0) {
        char *end = NULL;
        const long range = strtol(text + 10, &end, 10);
        while (end != NULL && *end != '\0' && isspace((unsigned char)*end)) ++end;
        if (end == NULL || *end != '\0' || range < 0 ||
            range >= SKYORB_RANGE_COUNT) {
            snprintf(response, response_size, "ERR range must be 0..5\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        config.range_index = (uint8_t)range;
        ble_store_skyorb_config(&config);
        snprintf(response, response_size, "OK RANGE %ld\r\n", range);
        return ESP_OK;
    }
    if (strcasecmp(text, "SET AUTO_LOCATION") == 0) {
        config.location_valid = false;
        config.location_auto = true;
        s_skyorb_auto_location_attempted = false;
        s_skyorb_location_result = SKYORB_LOCATION_REQUESTED;
        ble_store_skyorb_config(&config);
        snprintf(response, response_size, "OK AUTO_LOCATION\r\n");
        return ESP_OK;
    }
    if (strncasecmp(text, "SET LOCATION ", 13U) == 0) {
        float latitude = 0.0f;
        float longitude = 0.0f;
        char extra = '\0';
        if (sscanf(text + 13, "%f %f %c", &latitude, &longitude, &extra) != 2 ||
            !isfinite(latitude) || !isfinite(longitude) ||
            fabsf(latitude) > 90.0f || fabsf(longitude) > 180.0f) {
            snprintf(response, response_size, "ERR invalid latitude/longitude\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        config.latitude = latitude;
        config.longitude = longitude;
        config.location_valid = true;
        config.location_auto = false;
        s_skyorb_location_result = SKYORB_LOCATION_READY;
        ble_store_skyorb_config(&config);
        snprintf(response, response_size, "OK LOCATION %.6f %.6f\r\n",
                 (double)latitude, (double)longitude);
        return ESP_OK;
    }
    if (strncasecmp(text, "SET WIFI ", 9U) == 0) {
        const size_t credentials_length = strlen(text + 9);
        if (credentials_length >= sizeof(s_ble_wifi_credentials)) {
            snprintf(response, response_size, "ERR invalid Wi-Fi credentials\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(s_ble_wifi_credentials, text + 9, credentials_length + 1U);
        char *separator = strchr(s_ble_wifi_credentials, '|');
        if (separator == NULL) {
            snprintf(response, response_size, "ERR WIFI format is ssid|password\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        *separator++ = '\0';
        ble_trim_ascii(s_ble_wifi_credentials);
        ble_trim_ascii(separator);
        if (s_ble_wifi_credentials[0] == '\0' ||
            strlen(s_ble_wifi_credentials) > 32U || strlen(separator) > 64U) {
            snprintf(response, response_size, "ERR invalid Wi-Fi credentials\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        if (!wifi_config_upsert_profile(&config, s_ble_wifi_credentials, separator)) {
            snprintf(response, response_size, "ERR Wi-Fi list full (max %u)\r\n",
                     (unsigned)WIFI_PROFILE_MAX);
            return ESP_ERR_NO_MEM;
        }
        ble_store_skyorb_config(&config);
        memset(s_wifi_profile_visible, 0, sizeof(s_wifi_profile_visible));
        memset(s_wifi_profile_ap_valid, 0, sizeof(s_wifi_profile_ap_valid));
        s_wifi_scan_requested = s_skyorb_wifi_enabled;
        snprintf(response, response_size, "OK WIFI SAVED %u\r\n",
                 (unsigned)config.wifi_selected);
        return ESP_OK;
    }
    if (strncasecmp(text, "SELECT WIFI ", 12U) == 0) {
        char *end = NULL;
        const long index = strtol(text + 12, &end, 10);
        while (end != NULL && *end != '\0' && isspace((unsigned char)*end)) ++end;
        if (end == NULL || *end != '\0' || index < 0 || index >= config.wifi_count) {
            snprintf(response, response_size, "ERR invalid Wi-Fi index\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        if (!wifi_request_profile_connection((uint8_t)index)) {
            snprintf(response, response_size, "ERR Wi-Fi switch unavailable\r\n");
            return ESP_ERR_INVALID_STATE;
        }
        snprintf(response, response_size, "OK WIFI SELECTED %ld\r\n", index);
        return ESP_OK;
    }
    if (strncasecmp(text, "DELETE WIFI ", 12U) == 0) {
        char *end = NULL;
        const long index = strtol(text + 12, &end, 10);
        while (end != NULL && *end != '\0' && isspace((unsigned char)*end)) ++end;
        if (end == NULL || *end != '\0' || index < 0 || index >= config.wifi_count) {
            snprintf(response, response_size, "ERR invalid Wi-Fi index\r\n");
            return ESP_ERR_INVALID_ARG;
        }
        const bool deleted_active = config.wifi_selected == index;
        (void)wifi_config_delete_profile(&config, (uint8_t)index);
        ble_store_skyorb_config(&config);
        if (deleted_active) (void)pacon_save_switch("wifi_join", false);
        if (deleted_active && s_skyorb_wifi_connected) (void)esp_wifi_disconnect();
        memset(s_wifi_profile_visible, 0, sizeof(s_wifi_profile_visible));
        memset(s_wifi_profile_ap_valid, 0, sizeof(s_wifi_profile_ap_valid));
        s_wifi_scan_requested = s_skyorb_wifi_enabled;
        snprintf(response, response_size, "OK WIFI DELETED %ld\r\n", index);
        return ESP_OK;
    }
    snprintf(response, response_size, "ERR unsupported command\r\n");
    return ESP_ERR_NOT_SUPPORTED;
}

static bool skyorb_connect_saved_station(void)
{
    if (!s_skyorb_network_started || s_skyorb_mutex == NULL) return false;
    skyorb_config_t config;
    xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
    config = s_skyorb_config;
    xSemaphoreGive(s_skyorb_mutex);
    if (!config.wifi_valid) return false;

    if (s_skyorb_sta_netif == NULL) {
        s_skyorb_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_skyorb_sta_netif == NULL) {
            ESP_LOGE(TAG, "SkyOrb: STA netif allocation failed");
            return false;
        }
    }

    wifi_config_t station = {0};
    const size_t ssid_length = strnlen(config.ssid, sizeof(config.ssid));
    const size_t password_length = strnlen(config.password, sizeof(config.password));
    memcpy(station.sta.ssid, config.ssid,
           ssid_length < sizeof(station.sta.ssid) ? ssid_length : sizeof(station.sta.ssid));
    memcpy(station.sta.password, config.password,
           password_length < sizeof(station.sta.password) ? password_length : sizeof(station.sta.password));
    /* Accept the AP's advertised security mode.  Requiring WPA2 here rejects
     * some WPA/WPA2 mixed-mode routers before authentication even starts. */
    station.sta.threshold.authmode = WIFI_AUTH_OPEN;
    station.sta.pmf_cfg.capable = true;
    station.sta.pmf_cfg.required = false;
    const uint8_t selected = config.wifi_selected;
    if (selected < WIFI_PROFILE_MAX && s_wifi_profile_ap_valid[selected]) {
        station.sta.bssid_set = true;
        memcpy(station.sta.bssid, s_wifi_profile_ap[selected].bssid,
               sizeof(station.sta.bssid));
        station.sta.channel = s_wifi_profile_ap[selected].primary;
        ESP_LOGI(TAG,
                 "[WIFI-LINK] target BSSID=%02X:%02X:%02X:%02X:%02X:%02X channel=%u auth=%d pairwise=%d group=%d rssi=%d",
                 station.sta.bssid[0], station.sta.bssid[1], station.sta.bssid[2],
                 station.sta.bssid[3], station.sta.bssid[4], station.sta.bssid[5],
                 (unsigned)station.sta.channel,
                 (int)s_wifi_profile_ap[selected].authmode,
                 (int)s_wifi_profile_ap[selected].pairwise_cipher,
                 (int)s_wifi_profile_ap[selected].group_cipher,
                 (int)s_wifi_profile_ap[selected].rssi);
    } else {
        ESP_LOGW(TAG, "[WIFI-LINK] no saved scan record; connecting by SSID only");
    }
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &station);
    s_wifi_connect_started = xTaskGetTickCount();
    if (err == ESP_OK) err = esp_wifi_connect();
    s_skyorb_wifi_connected = false;
    s_skyorb_network_status = err;
    s_wifi_link_state = err == ESP_OK ? WIFI_LINK_CONNECTING : WIFI_LINK_FAILED;
    skyorb_mark_dirty();
    ESP_LOGI(TAG, "SkyOrb: joining saved Wi-Fi %s: %s",
             config.ssid, esp_err_to_name(err));
    return err == ESP_OK;
}

static bool wifi_request_profile_connection(uint8_t index)
{
    skyorb_config_t config = {0};
    ble_snapshot_skyorb_config(&config);
    if (index >= config.wifi_count) return false;

    wifi_config_select_profile(&config, index);
    ble_store_skyorb_config(&config);
    (void)pacon_save_switch("wifi_join", true);

    /* Stop the old station link's disconnect callback from racing a reconnect while the
     * selected profile is being changed.  The network task owns the actual
     * disconnect/scan/connect sequence. */
    s_wifi_should_connect = false;
    s_wifi_connect_requested = false;
    s_wifi_connect_after_scan = s_skyorb_wifi_enabled;
    s_wifi_scan_requested = s_skyorb_wifi_enabled;
    s_wifi_connect_started = 0;
    s_skyorb_disconnect_reason = 0;
    s_wifi_link_state = s_skyorb_wifi_enabled ? WIFI_LINK_CONNECTING : WIFI_LINK_IDLE;
    s_wifi_delete_profile = -1;
    s_wifi_settings_dirty = true;
    skyorb_mark_dirty();
    ESP_LOGI(TAG, "[WIFI-SWITCH] queued profile %u (%s): disconnect -> scan -> connect",
             (unsigned)index, config.ssid);
    return true;
}

static void skyorb_disable_network(void)
{
    s_skyorb_wifi_enabled = false;
    (void)pacon_save_switch("wifi_on", false);
    s_wifi_should_connect = false;
    s_wifi_connect_requested = false;
    s_wifi_connect_after_scan = false;
    s_wifi_scan_requested = false;

    (void)esp_wifi_disconnect();
    const esp_err_t err = esp_wifi_stop();
    s_skyorb_network_started = false;
    s_skyorb_wifi_connected = false;
    s_skyorb_disconnect_reason = 0;
    s_wifi_scan_in_progress = false;
    s_wifi_scan_status = ESP_ERR_INVALID_STATE;
    memset(s_wifi_profile_visible, 0, sizeof(s_wifi_profile_visible));
    memset(s_wifi_profile_ap_valid, 0, sizeof(s_wifi_profile_ap_valid));
    s_wifi_link_state = WIFI_LINK_IDLE;
    s_skyorb_last_fetch = 0;
    s_skyorb_fetch_failed = false;
    s_skyorb_fetch_failure_stage = NULL;
    s_skyorb_network_status = (err == ESP_ERR_INVALID_STATE) ? ESP_OK : err;
    skyorb_mark_dirty();
    ESP_LOGI(TAG, "SkyOrb: Wi-Fi disabled; radar network paused (%s)",
             err == ESP_ERR_INVALID_STATE ? "already stopped" : esp_err_to_name(err));
}

static void skyorb_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        s_wifi_link_state = WIFI_LINK_ASSOCIATED;
        s_skyorb_disconnect_reason = 0;
        skyorb_mark_dirty();
        ESP_LOGI(TAG, "[WIFI-LINK] associated with AP; waiting for DHCP address");
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected =
            (const wifi_event_sta_disconnected_t *)event_data;
        s_skyorb_disconnect_reason = disconnected != NULL ? disconnected->reason : 0;
        s_skyorb_wifi_connected = false;
        const bool authentication_failed =
            s_skyorb_disconnect_reason == WIFI_REASON_AUTH_EXPIRE ||
            s_skyorb_disconnect_reason == WIFI_REASON_AUTH_FAIL ||
            s_skyorb_disconnect_reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
            s_skyorb_disconnect_reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
            s_skyorb_disconnect_reason == WIFI_REASON_CONNECTION_FAIL;
        const bool no_ap =
            s_skyorb_disconnect_reason == WIFI_REASON_NO_AP_FOUND ||
            s_skyorb_disconnect_reason == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
            s_skyorb_disconnect_reason == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD ||
            s_skyorb_disconnect_reason == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD;
        if (authentication_failed) {
            s_wifi_link_state = WIFI_LINK_AUTH_FAILED;
            s_wifi_should_connect = false;
        } else if (no_ap) {
            s_wifi_link_state = WIFI_LINK_NO_AP;
            s_wifi_should_connect = false;
        } else if (s_wifi_should_connect) {
            s_wifi_link_state = WIFI_LINK_CONNECTING;
        } else if (s_wifi_link_state != WIFI_LINK_TIMEOUT &&
                   s_wifi_link_state != WIFI_LINK_FAILED) {
            s_wifi_link_state = WIFI_LINK_IDLE;
        }
        skyorb_mark_dirty();
        if (s_skyorb_wifi_enabled && s_skyorb_network_started && s_wifi_should_connect) {
            const esp_err_t retry = esp_wifi_connect();
            ESP_LOGW(TAG, "[WIFI-LINK] disconnected reason=%u; reconnect=%s",
                     (unsigned)s_skyorb_disconnect_reason, esp_err_to_name(retry));
        } else {
            ESP_LOGW(TAG, "[WIFI-LINK] disconnected reason=%u; retry stopped state=%d",
                     (unsigned)s_skyorb_disconnect_reason, (int)s_wifi_link_state);
        }
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_skyorb_disconnect_reason = 0;
        s_skyorb_wifi_connected = true;
        s_wifi_should_connect = false;
        s_wifi_link_state = WIFI_LINK_CONNECTED;
        s_skyorb_last_fetch = 0;
        s_skyorb_fetch_failed = false;
        s_skyorb_fetch_failure_stage = NULL;
        s_skyorb_auto_location_attempted = false;
        skyorb_mark_dirty();
        ESP_LOGI(TAG, "[WIFI-LINK] GOT_IP; location/ADS-B refresh enabled");
    }
}

static void wifi_scan_saved_network(void)
{
    s_wifi_scan_in_progress = true;
    s_wifi_scan_status = ESP_ERR_INVALID_STATE;
    memset(s_wifi_profile_visible, 0, sizeof(s_wifi_profile_visible));
    memset(s_wifi_profile_ap_valid, 0, sizeof(s_wifi_profile_ap_valid));
    for (uint8_t index = 0; index < WIFI_PROFILE_MAX; ++index) {
        s_wifi_profile_rssi[index] = -127;
    }
    s_wifi_settings_dirty = true;

    wifi_scan_config_t scan = {0};
    scan.show_hidden = true;
    scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err == ESP_OK) {
        uint16_t count = 16;
        wifi_ap_record_t records[16] = {0};
        err = esp_wifi_scan_get_ap_records(&count, records);
        if (err == ESP_OK) {
            skyorb_config_t config = {0};
            xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
            config = s_skyorb_config;
            xSemaphoreGive(s_skyorb_mutex);
            for (uint16_t index = 0; index < count; ++index) {
                for (uint8_t profile = 0; profile < config.wifi_count; ++profile) {
                    if (strcmp((const char *)records[index].ssid,
                               config.wifi_profiles[profile].ssid) != 0) continue;
                    if (!s_wifi_profile_visible[profile] ||
                        records[index].rssi > s_wifi_profile_rssi[profile]) {
                        s_wifi_profile_visible[profile] = true;
                        s_wifi_profile_rssi[profile] = records[index].rssi;
                        s_wifi_profile_ap[profile] = records[index];
                        s_wifi_profile_ap_valid[profile] = true;
                    }
                    ESP_LOGI(TAG,
                             "[WIFI-SCAN] saved[%u] SSID=%s BSSID=%02X:%02X:%02X:%02X:%02X:%02X channel=%u auth=%d rssi=%d",
                             (unsigned)profile, records[index].ssid,
                             records[index].bssid[0], records[index].bssid[1],
                             records[index].bssid[2], records[index].bssid[3],
                             records[index].bssid[4], records[index].bssid[5],
                             (unsigned)records[index].primary,
                             (int)records[index].authmode, (int)records[index].rssi);
                    break;
                }
            }
        }
    }
    s_wifi_scan_status = err;
    s_wifi_scan_in_progress = false;
    s_wifi_settings_dirty = true;
    unsigned visible_count = 0;
    skyorb_config_t config = {0};
    xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
    config = s_skyorb_config;
    xSemaphoreGive(s_skyorb_mutex);
    for (uint8_t index = 0; index < config.wifi_count; ++index) {
        if (s_wifi_profile_visible[index]) ++visible_count;
    }
    ESP_LOGI(TAG, "Wi-Fi scan complete: %s saved_visible=%u/%u",
             esp_err_to_name(err), visible_count, (unsigned)config.wifi_count);
}

static bool skyorb_network_failure(const char *stage, esp_err_t err)
{
    s_skyorb_network_started = false;
    s_skyorb_network_status = err;
    s_skyorb_fetch_failed = true;
    skyorb_mark_dirty();
    ESP_LOGE(TAG, "Wi-Fi STA: %s failed: %s (0x%X)", stage, esp_err_to_name(err),
             (unsigned)err);
    return false;
}

static bool skyorb_start_network(void)
{
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "[WIFI-DBG] start begin: started=%d connected=%d task_started=%d",
             (int)s_skyorb_network_started,
             (int)s_skyorb_wifi_connected, (int)s_skyorb_network_task_started);
    ESP_LOGI(TAG, "[WIFI-DBG] internal heap before start: free=%u largest=%u",
             (unsigned)internal_free, (unsigned)internal_largest);
    if (s_skyorb_network_started) return true;
    s_skyorb_network_status = ESP_ERR_INVALID_STATE;
    skyorb_mark_dirty();

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_NVS_INVALID_STATE) {
        return skyorb_network_failure("NVS init", err);
    }
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return skyorb_network_failure("netif init", err);
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return skyorb_network_failure("event loop", err);
    }
    if (s_skyorb_sta_netif == NULL) {
        ESP_LOGI(TAG, "[WIFI-DBG] creating STA netif");
        s_skyorb_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_skyorb_sta_netif == NULL) {
            return skyorb_network_failure("STA netif creation", ESP_ERR_NO_MEM);
        }
    }
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    /* PACON keeps a large QSPI DMA working set, BLE, and the NAND reader
     * alive while Wi-Fi is enabled.  The generated defaults are intentionally
     * conservative, but make the values explicit here as well so an older
     * sdkconfig cannot silently restore the 16/32-buffer profile. */
    /* Two RX buffers were enough to scan but repeatedly lost authentication
     * exchanges (AUTH_EXPIRE / CONNECTION_FAIL).  Keep this far below the
     * IDF defaults while allowing a complete WPA handshake. */
    wifi_init.static_rx_buf_num = 4;
    wifi_init.dynamic_rx_buf_num = 8;
    wifi_init.static_tx_buf_num = 6;
    wifi_init.dynamic_tx_buf_num = 0;
    wifi_init.cache_tx_buf_num = 8;
    wifi_init.rx_mgmt_buf_num = 4;
    wifi_init.rx_ba_win = 4;
    wifi_init.mgmt_sbuf_num = 6; /* IDF minimum */
    ESP_LOGI(TAG, "[WIFI-DBG] init buffers: static_rx=%d dynamic_rx=%d static_tx=%d cache_tx=%d rx_mgmt=%d ba=%d mgmt_sbuf=%d",
             wifi_init.static_rx_buf_num, wifi_init.dynamic_rx_buf_num,
             wifi_init.static_tx_buf_num, wifi_init.cache_tx_buf_num,
             wifi_init.rx_mgmt_buf_num, wifi_init.rx_ba_win,
             wifi_init.mgmt_sbuf_num);
    ESP_LOGI(TAG, "[WIFI-DBG] initializing Wi-Fi driver");
    err = esp_wifi_init(&wifi_init);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Wi-Fi STA: driver was already initialized; reusing it");
    } else if (err != ESP_OK) {
        return skyorb_network_failure("Wi-Fi driver init", err);
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return skyorb_network_failure("Wi-Fi storage", err);
    }
    if (!s_skyorb_wifi_events_registered) {
        (void)esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, skyorb_wifi_event, NULL);
        (void)esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, skyorb_wifi_event, NULL);
        s_skyorb_wifi_events_registered = true;
    }
    /* The watch-style Wi-Fi page uses STA-only mode. Provisioning is handled
     * over BLE or from saved profiles on the board. */
    wifi_country_t country = {
        .cc = "CN",
        .schan = 1,
        .nchan = 13,
        .max_tx_power = 80,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    err = esp_wifi_set_country(&country);
    if (err != ESP_OK) return skyorb_network_failure("set CN RF domain", err);
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return skyorb_network_failure("set STA mode", err);
    err = esp_wifi_start();
    bool driver_already_running = false;
    if (err == ESP_ERR_INVALID_STATE) {
        wifi_mode_t mode = WIFI_MODE_NULL;
        if (esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_STA) {
            driver_already_running = true;
            err = ESP_OK;
            ESP_LOGW(TAG, "Wi-Fi STA was already started; reusing driver");
        }
    }
    if (err != ESP_OK) {
        return skyorb_network_failure("STA start", err);
    }
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        return skyorb_network_failure("disable STA power save", err);
    }
    s_skyorb_network_started = true;
    if (driver_already_running) s_skyorb_network_status = ESP_OK;
    skyorb_load_config();
    wifi_mode_t mode = WIFI_MODE_NULL;
    const esp_err_t mode_err = esp_wifi_get_mode(&mode);
    ESP_LOGI(TAG, "Wi-Fi STA: start=%s mode=%s/%d; waiting for scan/selection",
             driver_already_running ? "already-running" : "requested",
             esp_err_to_name(mode_err), (int)mode);
    return true;
}

/* Bring up STA-only networking. Provisioning is handled over BLE or the
 * board's saved-profile settings page. */
static bool skyorb_start_requested_network(void)
{
    return skyorb_start_network();
}

static float skyorb_json_number(const cJSON *object, const char *name, float fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : fallback;
}

static void skyorb_json_text(const cJSON *object, const char *name, char *out, size_t out_size)
{
    if (out_size == 0) return;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "%s", item->valuestring);
    size_t length = strlen(out);
    while (length > 0 && isspace((unsigned char)out[length - 1])) out[--length] = '\0';
}

static bool skyorb_fetch_aircraft(void)
{
    skyorb_config_t config;
    xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
    config = s_skyorb_config;
    const uint8_t range_index = s_skyorb_range_index;
    xSemaphoreGive(s_skyorb_mutex);
    if (!config.wifi_valid || !config.location_valid || !s_skyorb_wifi_connected) {
        s_skyorb_fetch_failure_stage = "STATE RETRY";
        ESP_LOGW(TAG, "[SKYORB-FETCH] stage=state wifi_valid=%d location_valid=%d connected=%d",
                 (int)config.wifi_valid, (int)config.location_valid,
                 (int)s_skyorb_wifi_connected);
        return false;
    }
    const float fetch_km = s_skyorb_ranges_km[range_index] * 1.6f;
    const float fetch_nm = fetch_km / 1.852f;
    char url[160];
    /* ADSB.lol exposes the same readsb-compatible `ac` response used by this
     * parser.  Airplanes.live currently returns a project-approval HTTP 403 to
     * this deployment, so do not hammer that endpoint with retries. */
    snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%.5f/%.5f/%.1f",
             (double)config.latitude, (double)config.longitude, (double)fetch_nm);
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 7000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        s_skyorb_fetch_failure_stage = "MEM RETRY";
        ESP_LOGE(TAG, "[SKYORB-FETCH] stage=client_init free_internal=%u largest_internal=%u free_psram=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        return false;
    }
    (void)esp_http_client_set_header(client, "User-Agent",
                                    "PaconRadar/1.0 (+https://github.com/rmjskhy/Pacon)");
    (void)esp_http_client_set_header(client, "Accept", "application/json");
    esp_err_t err = esp_http_client_open(client, 0);
    int header_length = err == ESP_OK ? esp_http_client_fetch_headers(client) : -1;
    if (err != ESP_OK || header_length > 65536) {
        s_skyorb_fetch_failure_stage = "NET/TLS RETRY";
        ESP_LOGW(TAG, "[SKYORB-FETCH] stage=open err=%s(0x%X) headers=%d free_internal=%u largest_internal=%u free_psram=%u url=%s",
                 esp_err_to_name(err), (unsigned)err, header_length,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), url);
        esp_http_client_cleanup(client);
        return false;
    }
    size_t capacity = header_length > 0 ? (size_t)header_length + 1 : 32769;
    char *body = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) body = malloc(capacity);
    if (body == NULL) {
        s_skyorb_fetch_failure_stage = "MEM RETRY";
        ESP_LOGE(TAG, "[SKYORB-FETCH] stage=body_alloc bytes=%u", (unsigned)capacity);
        esp_http_client_cleanup(client);
        return false;
    }
    size_t total = 0;
    while (total + 1 < capacity) {
        int read = esp_http_client_read(client, body + total, capacity - total - 1);
        if (read < 0) {
            s_skyorb_fetch_failure_stage = "READ RETRY";
            ESP_LOGW(TAG, "[SKYORB-FETCH] stage=body_read err=%d bytes=%u",
                     read, (unsigned)total);
            total = 0;
            break;
        }
        if (read == 0) break;
        total += (size_t)read;
    }
    body[total] = '\0';
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (status != 200 || total == 0) {
        s_skyorb_fetch_failure_stage = status == 200 ? "READ RETRY" : "HTTP RETRY";
        const size_t preview_length = total < 120U ? total : 120U;
        ESP_LOGW(TAG, "[SKYORB-FETCH] stage=http status=%d body=%u preview=%.*s", status,
                 (unsigned)total, (int)preview_length, body);
        free(body);
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(body, total);
    free(body);
    if (root == NULL) {
        s_skyorb_fetch_failure_stage = "JSON RETRY";
        ESP_LOGW(TAG, "[SKYORB-FETCH] stage=json_parse bytes=%u", (unsigned)total);
        return false;
    }
    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "ac");
    skyorb_aircraft_t updated[SKYORB_MAX_AIRCRAFT] = {0};
    size_t count = 0;
    cJSON *plane;
    cJSON_ArrayForEach(plane, list) {
        if (count >= SKYORB_MAX_AIRCRAFT || !cJSON_IsObject(plane)) continue;
        cJSON *latitude = cJSON_GetObjectItemCaseSensitive(plane, "lat");
        cJSON *longitude = cJSON_GetObjectItemCaseSensitive(plane, "lon");
        if (!cJSON_IsNumber(latitude) || !cJSON_IsNumber(longitude)) continue;
        skyorb_aircraft_t *aircraft = &updated[count];
        aircraft->lat = (float)latitude->valuedouble;
        aircraft->lon = (float)longitude->valuedouble;
        aircraft->heading_deg = skyorb_json_number(plane, "true_heading",
                                  skyorb_json_number(plane, "track", 0.0f));
        aircraft->track_deg = skyorb_json_number(plane, "track", aircraft->heading_deg);
        aircraft->speed_knots = skyorb_json_number(plane, "gs", 0.0f);
        skyorb_json_text(plane, "flight", aircraft->callsign, sizeof(aircraft->callsign));
        if (aircraft->callsign[0] == '\0') skyorb_json_text(plane, "hex", aircraft->callsign,
                                                               sizeof(aircraft->callsign));
        const cJSON *altitude = cJSON_GetObjectItemCaseSensitive(plane, "alt_baro");
        if (cJSON_IsNumber(altitude)) {
            snprintf(aircraft->altitude, sizeof(aircraft->altitude), "%.0fft", altitude->valuedouble);
        } else if (cJSON_IsString(altitude) && altitude->valuestring != NULL) {
            snprintf(aircraft->altitude, sizeof(aircraft->altitude), "%s", altitude->valuestring);
        }
        ++count;
    }
    cJSON_Delete(root);
    xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
    memcpy(s_skyorb_aircraft, updated, count * sizeof(updated[0]));
    s_skyorb_aircraft_count = count;
    s_skyorb_demo_mode = false;
    s_skyorb_fetch_failed = false;
    s_skyorb_fetch_failure_stage = NULL;
    s_skyorb_last_success = xTaskGetTickCount();
    xSemaphoreGive(s_skyorb_mutex);
    skyorb_mark_dirty();
    ESP_LOGI(TAG, "SkyOrb: refreshed %u aircraft", (unsigned)count);
    return true;
}

/* This is deliberately named network-IP location rather than Wi-Fi location:
 * ESP32 can scan BSSIDs, but accurate BSSID positioning services require an
 * API key and a third-party Wi-Fi database.  A single HTTPS lookup of the
 * connected network's public IP is key-free and gives a useful city-level
 * fallback.  Manual coordinates always override it. */
static skyorb_location_result_t skyorb_try_location_provider(const char *url,
                                                              bool require_success,
                                                              float *latitude,
                                                              float *longitude)
{
    const esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 7000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) return SKYORB_LOCATION_ERR_CLIENT;
    (void)esp_http_client_set_header(client, "User-Agent", "PACON/1.0");
    const esp_err_t err = esp_http_client_open(client, 0);
    const int64_t header_length = err == ESP_OK ? esp_http_client_fetch_headers(client) : -1;
    if (err != ESP_OK || header_length > 8192) {
        ESP_LOGW(TAG, "SkyOrb: location provider %s open failed: %s", url,
                 esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return SKYORB_LOCATION_ERR_OPEN;
    }
    char body[4096];
    size_t total = 0;
    bool read_failed = false;
    while (total + 1 < sizeof(body)) {
        const int read = esp_http_client_read(client, body + total, sizeof(body) - total - 1);
        if (read < 0) {
            read_failed = true;
            break;
        }
        if (read == 0) break;
        total += (size_t)read;
    }
    body[total] = '\0';
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (read_failed || total == 0) {
        ESP_LOGW(TAG, "SkyOrb: location provider %s returned no body", url);
        return SKYORB_LOCATION_ERR_RESPONSE;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "SkyOrb: location provider %s HTTP=%d", url, status);
        return SKYORB_LOCATION_ERR_HTTP;
    }
    cJSON *root = cJSON_ParseWithLength(body, total);
    const cJSON *ok = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "success");
    const float parsed_latitude = root == NULL ? NAN : skyorb_json_number(root, "latitude", NAN);
    const float parsed_longitude = root == NULL ? NAN : skyorb_json_number(root, "longitude", NAN);
    const bool valid = (!require_success || cJSON_IsTrue(ok)) &&
                       isfinite(parsed_latitude) && isfinite(parsed_longitude) &&
                       fabsf(parsed_latitude) <= 90.0f && fabsf(parsed_longitude) <= 180.0f;
    cJSON_Delete(root);
    if (!valid) {
        ESP_LOGW(TAG, "SkyOrb: location provider %s response was invalid", url);
        return SKYORB_LOCATION_ERR_PARSE;
    }
    *latitude = parsed_latitude;
    *longitude = parsed_longitude;
    return SKYORB_LOCATION_READY;
}

static skyorb_location_result_t skyorb_locate_from_network_ip(void)
{
    if (!s_skyorb_wifi_connected) return SKYORB_LOCATION_REQUESTED;
    static const struct {
        const char *url;
        bool require_success;
    } providers[] = {
        /* ipapi.co documents this key-free HTTPS endpoint for client IP lookup. */
        {"https://ipapi.co/json/", false},
        /* ipwhois documents its free endpoint as HTTP; it is a fallback for
         * networks on which the first provider is unreachable. */
        {"http://ipwho.is/", true},
    };
    float latitude = NAN;
    float longitude = NAN;
    skyorb_location_result_t result = SKYORB_LOCATION_ERR_OPEN;
    for (size_t i = 0; i < sizeof(providers) / sizeof(providers[0]); ++i) {
        result = skyorb_try_location_provider(providers[i].url,
                                              providers[i].require_success,
                                              &latitude, &longitude);
        if (result == SKYORB_LOCATION_READY) break;
    }
    if (result != SKYORB_LOCATION_READY) return result;
    skyorb_config_t config = {0};
    xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
    config = s_skyorb_config;
    /* A manual save which raced this short request must win. */
    if (!config.location_valid || config.location_auto) {
        config.latitude = latitude;
        config.longitude = longitude;
        config.location_valid = true;
        config.location_auto = true;
        s_skyorb_config = config;
    }
    xSemaphoreGive(s_skyorb_mutex);
    skyorb_save_config(&config);
    skyorb_mark_dirty();
    ESP_LOGI(TAG, "SkyOrb: automatic network location %.4f, %.4f", (double)config.latitude,
             (double)config.longitude);
    return SKYORB_LOCATION_READY;
}

static void skyorb_network_task(void *argument)
{
    (void)argument;
    ESP_LOGI(TAG, "[WIFI-DBG] network task entered; starting Wi-Fi once on dedicated task");
    if (!skyorb_start_requested_network()) {
        s_skyorb_fetch_failed = true;
        skyorb_mark_dirty();
        s_skyorb_network_task_started = false;
        ESP_LOGE(TAG, "[WIFI-DBG] network task stopped because Wi-Fi startup failed");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "[WIFI-DBG] network task startup complete; entering service loop");
    /* Serialize STA scan/connect operations in this task so switching saved
     * profiles cannot race Wi-Fi event callbacks. */
    while (true) {
        if (!s_skyorb_wifi_enabled) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        /* The task deliberately survives an explicit Wi-Fi OFF so a later ON
         * does not need another large stack allocation. */
        if (!s_skyorb_network_started) {
            ESP_LOGI(TAG, "[WIFI-DBG] Wi-Fi re-enabled; restarting network on existing task");
            if (!skyorb_start_requested_network()) {
                s_skyorb_fetch_failed = true;
                skyorb_mark_dirty();
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        }
        if (s_wifi_scan_requested &&
            (!s_skyorb_wifi_connected || s_wifi_connect_after_scan)) {
            s_wifi_scan_requested = false;
            if (s_wifi_connect_after_scan) {
                /* esp_wifi_set_config()+esp_wifi_connect() does not reliably
                 * switch away from an established AP.  Leave it first and
                 * allow the disconnect event to settle before scanning. */
                s_wifi_should_connect = false;
                const esp_err_t leave = esp_wifi_disconnect();
                if (leave == ESP_OK) vTaskDelay(pdMS_TO_TICKS(120));
                ESP_LOGI(TAG, "[WIFI-SWITCH] old link left: %s",
                         esp_err_to_name(leave));
            }
            wifi_scan_saved_network();
            if (s_wifi_connect_after_scan) {
                s_wifi_connect_after_scan = false;
                s_wifi_connect_requested = true;
                ESP_LOGI(TAG, "[WIFI-SWITCH] fresh scan complete; starting selected profile");
            }
        }
        if (s_wifi_connect_requested) {
            s_wifi_connect_requested = false;
            s_wifi_should_connect = true;
            s_skyorb_disconnect_reason = 0;
            if (!skyorb_connect_saved_station()) {
                s_wifi_should_connect = false;
            }
            s_wifi_settings_dirty = true;
        }
        const TickType_t now = xTaskGetTickCount();
        if (s_wifi_should_connect && !s_skyorb_wifi_connected &&
            s_wifi_connect_started != 0 &&
            (int32_t)(now - s_wifi_connect_started) >= pdMS_TO_TICKS(15000)) {
            s_wifi_should_connect = false;
            s_wifi_link_state = WIFI_LINK_TIMEOUT;
            (void)esp_wifi_disconnect();
            skyorb_mark_dirty();
            ESP_LOGW(TAG, "[WIFI-LINK] connection timed out after 15 seconds");
        }
        skyorb_config_t config = {0};
        xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
        config = s_skyorb_config;
        xSemaphoreGive(s_skyorb_mutex);
        if (s_skyorb_wifi_connected && !s_skyorb_auto_location_attempted &&
            (!config.location_valid || config.location_auto)) {
            s_skyorb_auto_location_attempted = true;
            s_skyorb_location_in_progress = true;
            s_skyorb_location_result = SKYORB_LOCATION_IN_PROGRESS;
            const skyorb_location_result_t located = skyorb_locate_from_network_ip();
            s_skyorb_location_result = located;
            s_skyorb_location_in_progress = false;
            if (located != SKYORB_LOCATION_READY) {
                /* Location discovery and aircraft-data retrieval are separate
                 * states.  A blocked IP-location service must lead the user
                 * to SET LOCATION, not masquerade as an ADS-B data failure. */
                skyorb_mark_dirty();
            }
        }
        const uint32_t fetch_interval_ms = s_skyorb_fetch_failed ?
                                           SKYORB_FETCH_RETRY_MS : SKYORB_FETCH_PERIOD_MS;
        if (s_skyorb_wifi_connected && config.location_valid &&
            !s_skyorb_fetch_in_progress &&
            (s_skyorb_last_fetch == 0 ||
             (int32_t)(now - s_skyorb_last_fetch) >= pdMS_TO_TICKS(fetch_interval_ms))) {
            s_skyorb_fetch_in_progress = true;
            s_skyorb_last_fetch = now;
            const bool success = skyorb_fetch_aircraft();
            if (!success) {
                s_skyorb_fetch_failed = true;
                skyorb_mark_dirty();
                ESP_LOGW(TAG, "[SKYORB-FETCH] failed stage=%s; retry_in=%u_ms",
                         s_skyorb_fetch_failure_stage != NULL ?
                         s_skyorb_fetch_failure_stage : "UNKNOWN RETRY",
                         (unsigned)SKYORB_FETCH_RETRY_MS);
            }
            s_skyorb_fetch_in_progress = false;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void skyorb_set_pixel(int x, int y, uint16_t colour)
{
    if (x >= 0 && x < LCD_WIDTH && y >= 0 && y < LCD_HEIGHT) {
        s_lcd_canvas[(size_t)y * LCD_WIDTH + x] = colour;
    }
}

static void skyorb_blend_pixel(int x, int y, uint16_t colour, uint8_t alpha)
{
    if (x >= 0 && x < LCD_WIDTH && y >= 0 && y < LCD_HEIGHT) {
        uint16_t *pixel = &s_lcd_canvas[(size_t)y * LCD_WIDTH + x];
        *pixel = rgb565_blend(*pixel, colour, alpha);
    }
}

static void skyorb_line(int x0, int y0, int x1, int y1, uint16_t colour, uint8_t alpha)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        skyorb_blend_pixel(x0, y0, colour, alpha);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

static void skyorb_circle_dot(int center_x, int center_y, int radius, uint16_t colour)
{
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) skyorb_set_pixel(center_x + x, center_y + y, colour);
        }
    }
}

static void skyorb_text_clipped(const char *text, int origin_x, int line_top,
                                 const lv_font_t *font, uint16_t colour,
                                 const dirty_rect_t *clip)
{
    /* Draw each glyph bitmap once.  The former implementation tested every
     * candidate canvas pixel against every character in the string, which
     * made Settings scrolling disproportionately expensive. */
    int pen_x = origin_x;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        lv_font_glyph_dsc_t glyph;
        const uint32_t next = (uint8_t)text[index + 1];
        if (!lv_font_get_glyph_dsc(font, &glyph, (uint8_t)text[index], next)) {
            continue;
        }
        const int glyph_left = pen_x + glyph.ofs_x;
        const int glyph_top = line_top + (font->line_height - font->base_line) -
                              glyph.box_h - glyph.ofs_y;
        const uint8_t *bitmap = lv_font_get_glyph_bitmap(glyph.resolved_font,
                                                          (uint8_t)text[index]);
        if (bitmap != NULL) {
            for (int gy = 0; gy < glyph.box_h; ++gy) {
                const int y = glyph_top + gy;
                if (y < 0 || y >= LCD_HEIGHT ||
                    (clip != NULL && (y < clip->y1 || y >= clip->y2))) {
                    continue;
                }
                for (int gx = 0; gx < glyph.box_w; ++gx) {
                    const int x = glyph_left + gx;
                    if (x < 0 || x >= LCD_WIDTH ||
                        (clip != NULL && (x < clip->x1 || x >= clip->x2))) {
                        continue;
                    }
                    const size_t pixel_index = (size_t)gy * glyph.box_w + (size_t)gx;
                    uint8_t alpha = 0;
                    if (glyph.bpp == 4) {
                        const uint8_t sample = bitmap[pixel_index >> 1];
                        alpha = (uint8_t)(((pixel_index & 1U) ?
                                          (sample & 0x0FU) : (sample >> 4)) * 17U);
                    } else if (glyph.bpp == 1) {
                        alpha = (bitmap[pixel_index >> 3] &
                                 (0x80U >> (pixel_index & 7U))) ? 255U : 0U;
                    }
                    if (alpha != 0) {
                        skyorb_blend_pixel(x, y, colour, alpha);
                    }
                }
            }
        }
        pen_x += glyph.adv_w;
    }
}

static void skyorb_text(const char *text, int origin_x, int line_top,
                         const lv_font_t *font, uint16_t colour)
{
    skyorb_text_clipped(text, origin_x, line_top, font, colour, NULL);
}

static int skyorb_text_width(const char *text, const lv_font_t *font)
{
    int width = 0;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        lv_font_glyph_dsc_t glyph;
        const uint32_t next = (uint8_t)text[index + 1];
        if (lv_font_get_glyph_dsc(font, &glyph, (uint8_t)text[index], next)) {
            width += glyph.adv_w;
        }
    }
    return width;
}

static void skyorb_text_centered(const char *text, int center_x, int line_top,
                                 const lv_font_t *font, uint16_t colour)
{
    skyorb_text(text, center_x - skyorb_text_width(text, font) / 2,
                line_top, font, colour);
}

static bool watch_text_visible_bounds(const char *text, const lv_font_t *font,
                                      int *visible_left, int *visible_right)
{
    if (text == NULL || font == NULL || visible_left == NULL || visible_right == NULL) {
        return false;
    }

    int pen_x = 0;
    int min_x = 0;
    int max_x = 0;
    bool found = false;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        lv_font_glyph_dsc_t glyph;
        const uint32_t next = (uint8_t)text[index + 1];
        if (!lv_font_get_glyph_dsc(font, &glyph, (uint8_t)text[index], next)) {
            continue;
        }
        if (glyph.box_w > 0 && glyph.box_h > 0) {
            const int left = pen_x + glyph.ofs_x;
            const int right = left + glyph.box_w;
            if (!found || left < min_x) {
                min_x = left;
            }
            if (!found || right > max_x) {
                max_x = right;
            }
            found = true;
        }
        pen_x += glyph.adv_w;
    }
    if (!found) {
        return false;
    }
    *visible_left = min_x;
    *visible_right = max_x;
    return true;
}

static void watch_text_optically_centered(const char *text, int center_x, int line_top,
                                          const lv_font_t *font, uint16_t colour)
{
    int visible_left = 0;
    int visible_right = 0;
    if (!watch_text_visible_bounds(text, font, &visible_left, &visible_right)) {
        skyorb_text_centered(text, center_x, line_top, font, colour);
        return;
    }

    const int origin_x = (2 * center_x - visible_left - visible_right + 1) / 2;
    skyorb_text(text, origin_x, line_top, font, colour);
}

static void skyorb_draw_plane(int x, int y, float heading, float track, float speed,
                               uint16_t plane_colour, uint16_t vector_colour)
{
    const float radians = heading * 0.01745329252f;
    const float sine = sinf(radians), cosine = cosf(radians);
    const int tip_x = x + (int)lroundf(sine * 11.0f);
    const int tip_y = y - (int)lroundf(cosine * 11.0f);
    const int tail_x = x - (int)lroundf(sine * 5.0f);
    const int tail_y = y + (int)lroundf(cosine * 5.0f);
    const int wing_x = (int)lroundf(cosine * 6.0f);
    const int wing_y = (int)lroundf(sine * 6.0f);
    skyorb_line(tip_x, tip_y, tail_x + wing_x, tail_y + wing_y, plane_colour, 255);
    skyorb_line(tip_x, tip_y, tail_x - wing_x, tail_y - wing_y, plane_colour, 255);
    skyorb_line(tail_x + wing_x, tail_y + wing_y, tail_x - wing_x, tail_y - wing_y, plane_colour, 255);
    if (speed > 4.0f) {
        const float track_rad = track * 0.01745329252f;
        const int length = clamp_int((int)(speed / 20.0f), 8, 45);
        skyorb_line(tip_x, tip_y, tip_x + (int)lroundf(sinf(track_rad) * length),
                     tip_y - (int)lroundf(cosf(track_rad) * length), vector_colour, 210);
    }
}

static void skyorb_snapshot_aircraft(skyorb_aircraft_t *aircraft, size_t *count,
                                     skyorb_config_t *config, bool *demo_mode)
{
    if (s_skyorb_mutex == NULL) {
        *count = s_skyorb_aircraft_count;
        memcpy(aircraft, s_skyorb_aircraft, *count * sizeof(aircraft[0]));
        *config = s_skyorb_config;
        *demo_mode = s_skyorb_demo_mode;
        return;
    }
    xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
    *count = s_skyorb_aircraft_count;
    memcpy(aircraft, s_skyorb_aircraft, *count * sizeof(aircraft[0]));
    *config = s_skyorb_config;
    *demo_mode = s_skyorb_demo_mode;
    xSemaphoreGive(s_skyorb_mutex);
}

static void skyorb_compose_canvas(void)
{
    const int center_x = LCD_WIDTH / 2;
    const int center_y = LCD_HEIGHT / 2;
    const int radar_radius = 230;
    const int radar_radius2 = radar_radius * radar_radius;
    const uint16_t background = rgb565(2, 10, 25);
    const uint16_t grid = rgb565(20, 115, 75);
    const uint16_t sweep = rgb565(49, 255, 174);
    const uint16_t plane_colour = rgb565(255, 83, 101);
    const uint16_t vector_colour = rgb565(255, 88, 210);
    const float sweep_rad = (float)s_skyorb_sweep_angle * 0.01745329252f;
    const float sweep_sine = sinf(sweep_rad);
    const float sweep_cosine = cosf(sweep_rad);
    const int ring_1 = radar_radius / 4;
    const int ring_2 = radar_radius / 2;
    const int ring_3 = radar_radius * 3 / 4;
    const int ring_1_sq = ring_1 * ring_1, ring_2_sq = ring_2 * ring_2;
    const int ring_3_sq = ring_3 * ring_3;
    for (int y = 0; y < LCD_HEIGHT; ++y) {
        uint16_t *line = s_lcd_canvas + (size_t)y * LCD_WIDTH;
        for (int x = 0; x < LCD_WIDTH; ++x) {
            const int dx = x - center_x;
            const int dy = y - center_y;
            const int d2 = dx * dx + dy * dy;
            if (d2 > radar_radius2) {
                line[x] = 0;
                continue;
            }
            uint16_t colour = rgb565(2 + clamp_int(15 - d2 / 4200, 0, 15) / 4,
                                      14 + clamp_int(70 - d2 / 1600, 0, 70) / 5,
                                      29 + clamp_int(110 - d2 / 1000, 0, 110) / 6);
            if (abs(d2 - ring_1_sq) < ring_1 * 2 || abs(d2 - ring_2_sq) < ring_2 * 2 ||
                abs(d2 - ring_3_sq) < ring_3 * 2 || abs(d2 - radar_radius2) < radar_radius * 2 ||
                abs(dx) <= 1 || abs(dy) <= 1) {
                colour = rgb565_blend(colour, grid, 115);
            }
            const float forward = sweep_sine * (float)dx - sweep_cosine * (float)dy;
            const float lateral = sweep_cosine * (float)dx + sweep_sine * (float)dy;
            if (forward > 0.0f && fabsf(lateral) < 2.2f) {
                colour = rgb565_blend(colour, sweep, 210);
            } else if (forward > 0.0f && fabsf(lateral) < 13.0f) {
                const int alpha = 52 - (int)fabsf(lateral) * 3;
                if (alpha > 0) colour = rgb565_blend(colour, sweep, (uint8_t)alpha);
            }
            line[x] = colour;
        }
    }
    skyorb_aircraft_t aircraft[SKYORB_MAX_AIRCRAFT] = {0};
    skyorb_config_t config = {0};
    bool demo = true;
    size_t count = 0;
    skyorb_snapshot_aircraft(aircraft, &count, &config, &demo);
    const float outer_km = s_skyorb_ranges_km[s_skyorb_range_index];
    const bool show_aircraft = s_skyorb_wifi_connected;
    if (show_aircraft && demo) {
        const float phase = (float)s_skyorb_sweep_angle * 0.01745329252f;
        const float demo_angle[] = {0.3f, 1.7f, 2.65f, 3.85f, 5.0f, 5.65f};
        /* Physical distances make all six range choices visibly different. */
        const float demo_distance_km[] = {2.2f, 7.4f, 14.0f, 23.0f, 36.0f, 47.0f};
        const char *names[] = {"PAC101", "MU512", "CA439", "CES288", "JD613", "HU721"};
        count = 6;
        for (size_t index = 0; index < count; ++index) {
            const float angle = demo_angle[index] + phase * (0.014f + (float)index * 0.002f);
            const bool inside_range = demo_distance_km[index] <= outer_km;
            const float radius = inside_range
                ? demo_distance_km[index] * (float)radar_radius / outer_km
                : (float)(radar_radius - 5);
            const int px = center_x + (int)lroundf(cosf(angle) * radius);
            const int py = center_y + (int)lroundf(sinf(angle) * radius);
            if (inside_range) {
                skyorb_draw_plane(px, py, angle * 57.3f + 90.0f, angle * 57.3f + 110.0f,
                                  180.0f + (float)index * 22.0f, plane_colour, vector_colour);
                skyorb_text(names[index], px + (px < center_x ? 12 : -48), py - 19,
                             &lv_font_montserrat_14, rgb565(230, 252, 244));
            } else {
                skyorb_circle_dot(px, py, 4, plane_colour);
            }
        }
    } else if (show_aircraft) {
        for (size_t index = 0; index < count; ++index) {
            const float latitude_scale = cosf(config.latitude * 0.01745329252f);
            const float dx_km = (aircraft[index].lon - config.longitude) * 111.0f * latitude_scale;
            const float dy_km = (aircraft[index].lat - config.latitude) * 111.0f;
            const float distance = sqrtf(dx_km * dx_km + dy_km * dy_km);
            if (distance < 0.02f) continue;
            float scale = (float)radar_radius / outer_km;
            if (distance > outer_km) scale = (float)(radar_radius - 5) / distance;
            const int px = center_x + (int)lroundf(dx_km * scale);
            const int py = center_y - (int)lroundf(dy_km * scale);
            if (distance > outer_km) {
                skyorb_circle_dot(px, py, 4, plane_colour);
                continue;
            }
            skyorb_draw_plane(px, py, aircraft[index].heading_deg, aircraft[index].track_deg,
                              aircraft[index].speed_knots, plane_colour, vector_colour);
            if (index < 9 && aircraft[index].callsign[0] != '\0') {
                skyorb_text(aircraft[index].callsign, px + (px < center_x ? 14 : -62), py - 19,
                             &lv_font_montserrat_14, rgb565(230, 252, 244));
            }
        }
    }
    skyorb_circle_dot(center_x, center_y, 4, rgb565(235, 255, 247));
    skyorb_text("N", center_x - 6, 12, &lv_font_montserrat_14, rgb565(231, 250, 243));
    skyorb_text("S", center_x - 5, 440, &lv_font_montserrat_14, rgb565(231, 250, 243));
    skyorb_text("W", 12, center_y - 8, &lv_font_montserrat_14, rgb565(231, 250, 243));
    skyorb_text("E", 449, center_y - 8, &lv_font_montserrat_14, rgb565(231, 250, 243));
    char ring_label[12];
    snprintf(ring_label, sizeof(ring_label), "%.1f KM", (double)(outer_km * 0.5f));
    skyorb_text(ring_label, center_x + ring_2 + 8, center_y + 8,
                &lv_font_montserrat_14, rgb565(94, 188, 143));
    char range_label[20];
    snprintf(range_label, sizeof(range_label), "RADIUS %u KM", (unsigned)outer_km);
    skyorb_text_centered(range_label, center_x, 390, &lv_font_montserrat_18,
                         rgb565(117, 234, 176));
    if (!s_skyorb_wifi_enabled) {
        skyorb_text_centered("OFFLINE", center_x, 111, &lv_font_montserrat_18,
                             rgb565(255, 190, 96));
        skyorb_text_centered("NO NETWORK", center_x, 138, &lv_font_montserrat_14,
                             rgb565(205, 214, 225));
    } else if (s_wifi_link_state == WIFI_LINK_AUTH_FAILED) {
        skyorb_text_centered("AUTH FAILED", center_x, 111, &lv_font_montserrat_18,
                             rgb565(255, 126, 118));
        skyorb_text_centered("CHECK PASSWORD", center_x, 138, &lv_font_montserrat_14,
                             rgb565(225, 214, 220));
    } else if (s_wifi_link_state == WIFI_LINK_NO_AP) {
        skyorb_text_centered("AP NOT FOUND", center_x, 111, &lv_font_montserrat_18,
                             rgb565(255, 178, 105));
    } else if (s_wifi_link_state == WIFI_LINK_TIMEOUT) {
        skyorb_text_centered("TIMEOUT", center_x, 111, &lv_font_montserrat_18,
                             rgb565(255, 178, 105));
        skyorb_text_centered("TAP NETWORK AGAIN", center_x, 138, &lv_font_montserrat_14,
                             rgb565(225, 214, 220));
    } else if (!s_skyorb_wifi_connected && !s_wifi_should_connect) {
        skyorb_text_centered("WI-FI ON", center_x, 111, &lv_font_montserrat_18,
                             rgb565(100, 210, 255));
        skyorb_text_centered("NOT CONNECTED", center_x, 138, &lv_font_montserrat_14,
                             rgb565(205, 214, 225));
    } else if (!s_skyorb_wifi_connected) {
        skyorb_text_centered(s_wifi_link_state == WIFI_LINK_ASSOCIATED ?
                             "GETTING IP" : "CONNECTING", center_x, 111,
                             &lv_font_montserrat_18, rgb565(255, 218, 100));
    } else if (!config.location_valid) {
        skyorb_text_centered("ONLINE", center_x, 111, &lv_font_montserrat_18,
                             rgb565(137, 239, 203));
        skyorb_text_centered("SET LOCATION", center_x, 420, &lv_font_montserrat_14,
                             rgb565(205, 214, 225));
    } else if (s_skyorb_fetch_in_progress) {
        skyorb_text_centered("UPDATING", center_x, 111, &lv_font_montserrat_14,
                             rgb565(137, 239, 203));
    } else if (s_skyorb_fetch_failed) {
        skyorb_text_centered(s_skyorb_fetch_failure_stage != NULL ?
                             s_skyorb_fetch_failure_stage : "DATA RETRY",
                             center_x, 111, &lv_font_montserrat_14,
                             rgb565(255, 161, 148));
    } else if (count == 0 && s_skyorb_last_success != 0) {
        skyorb_text_centered("NO AIRCRAFT", center_x, 105, &lv_font_montserrat_18,
                             rgb565(137, 239, 203));
        skyorb_text_centered("IN SELECTED RANGE", center_x, 132, &lv_font_montserrat_14,
                             rgb565(205, 224, 218));
    } else {
        char live[24];
        snprintf(live, sizeof(live), "LIVE %u", (unsigned)count);
        skyorb_text_centered(live, center_x, 111, &lv_font_montserrat_14,
                             rgb565(137, 239, 203));
    }
    (void)background;
}

static void skyorb_render_frame(void)
{
    if (s_lcd_canvas == NULL) return;
    skyorb_compose_canvas();
    const dirty_rect_t full = {.x1 = 0, .y1 = 0, .x2 = LCD_WIDTH, .y2 = LCD_HEIGHT};
    const bool flushed = flush_canvas_rect(&full);
    static TickType_t last_diagnostic;
    const TickType_t now = xTaskGetTickCount();
    if (!flushed || last_diagnostic == 0 ||
        (int32_t)(now - last_diagnostic) >= (int32_t)pdMS_TO_TICKS(10000)) {
        ESP_LOGI(TAG,
                 "[SKYORB-RENDER] flush=%d wifi=%d connected=%d count=%u "
                 "location=%d fetching=%d failed=%d sleeping=%d",
                 flushed, s_skyorb_wifi_enabled, s_skyorb_wifi_connected,
                 (unsigned)s_skyorb_aircraft_count, s_skyorb_config.location_valid,
                 s_skyorb_fetch_in_progress, s_skyorb_fetch_failed, s_display_sleeping);
        last_diagnostic = now;
    }
    s_skyorb_dirty = false;
}

static clock_time_t watch_read_time(void)
{
    clock_time_t time = s_clock_cached_time;
    if (!time.valid) {
        const uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000LL);
        time.second = (int)(uptime % 60U);
        time.minute = (int)((uptime / 60U) % 60U);
        time.hour = (int)((uptime / 3600U) % 24U);
        time.day = 1;
        time.month = 1;
        time.year = 2000;
    }
    return time;
}

static uint16_t watch_blob_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static int16_t watch_blob_i16(const uint8_t *bytes)
{
    return (int16_t)watch_blob_u16(bytes);
}

static void watch_draw_asset_layer(const uint8_t *asset_start,
                                   const uint8_t *asset_end,
                                   float clockwise_angle)
{
    const size_t asset_size = (size_t)(asset_end - asset_start);
    if (asset_size < 12U || memcmp(asset_start, "PCA1", 4) != 0) return;

    const int source_x = watch_blob_i16(asset_start + 4);
    const int source_y = watch_blob_i16(asset_start + 6);
    const int width = watch_blob_u16(asset_start + 8);
    const int height = watch_blob_u16(asset_start + 10);
    const size_t required = 12U + (size_t)width * (size_t)height * 3U;
    if (width <= 0 || height <= 0 || required > asset_size) return;

    const int canvas_x = (LCD_WIDTH - WATCH_SOURCE_SIZE) / 2;
    const int canvas_y = (LCD_HEIGHT - WATCH_SOURCE_SIZE) / 2;
    const uint8_t *pixels = asset_start + 12;
    const float sine = sinf(clockwise_angle);
    const float cosine = cosf(clockwise_angle);
    const bool unrotated = fabsf(clockwise_angle) < 0.0001f;

    const int first_x = unrotated ? source_x : 0;
    const int first_y = unrotated ? source_y : 0;
    const int last_x = unrotated ? source_x + width : WATCH_SOURCE_SIZE;
    const int last_y = unrotated ? source_y + height : WATCH_SOURCE_SIZE;
    for (int dy = first_y; dy < last_y; ++dy) {
        const int y = canvas_y + dy;
        if (y < 0 || y >= LCD_HEIGHT) continue;
        uint16_t *line = s_lcd_canvas + (size_t)y * LCD_WIDTH;
        for (int dx = first_x; dx < last_x; ++dx) {
            int sx = dx;
            int sy = dy;
            if (!unrotated) {
                const float centered_x = (float)dx - WATCH_SOURCE_SIZE / 2.0f;
                const float centered_y = (float)dy - WATCH_SOURCE_SIZE / 2.0f;
                sx = (int)lroundf(cosine * centered_x + sine * centered_y +
                                  WATCH_SOURCE_SIZE / 2.0f);
                sy = (int)lroundf(-sine * centered_x + cosine * centered_y +
                                  WATCH_SOURCE_SIZE / 2.0f);
            }
            if (sx < source_x || sx >= source_x + width ||
                sy < source_y || sy >= source_y + height) continue;
            const int x = canvas_x + dx;
            if (x < 0 || x >= LCD_WIDTH) continue;
            const uint8_t *pixel = pixels +
                ((size_t)(sy - source_y) * width + (size_t)(sx - source_x)) * 3U;
            const uint8_t alpha = pixel[2];
            if (alpha == 0) continue;
            const uint16_t foreground = watch_blob_u16(pixel);
            line[x] = alpha == 255 ? foreground :
                      rgb565_blend(line[x], foreground, alpha);
        }
    }
}

static void watch_draw_character_layer(void)
{
    watch_draw_asset_layer(kkd1_character_start, kkd1_character_end, 0.0f);
}

static void watch_draw_time_dials(const clock_time_t *display_time)
{
    const float tau = 6.28318531f;
    const float minute_rotation = WATCH_MINUTE_POINTER_ANGLE +
                                  (float)display_time->minute * tau / 60.0f;
    const float hour_rotation = WATCH_HOUR_POINTER_ANGLE -
                                (float)(display_time->hour % 12) * tau / 12.0f;
    const float second_rotation = (float)display_time->second * tau / 60.0f;

    /* Exact APK layer order and transforms: minute scale, seconds scale,
     * Roman-hour scale, then the static mechanical centre. */
    watch_draw_asset_layer(kkd1_minute_dial_start, kkd1_minute_dial_end,
                           minute_rotation);
    watch_draw_asset_layer(kkd1_second_dial_start, kkd1_second_dial_end,
                           second_rotation);
    watch_draw_asset_layer(kkd1_hour_dial_start, kkd1_hour_dial_end,
                           hour_rotation);
    watch_draw_asset_layer(kkd1_mechanism_start, kkd1_mechanism_end, 0.0f);
}

#define WATCH_KKD1_COMPLICATION_CENTER_X 161
#define WATCH_KKD1_TIME_TOP 160
#define WATCH_KKD1_DATE_TOP 187
#define WATCH_KKD2_COMPLICATION_CENTER_X 161
#define WATCH_KKD2_TIME_TOP 162
#define WATCH_KKD2_DATE_TOP 188

static void watch_compose_kkd2(const clock_time_t *time)
{
    memset(s_lcd_canvas, 0, LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t));
    const float tau = 6.28318531f;
    /* Match the original KKD2 XML exactly: HOUR_0_23 * 30.  The source face
     * advances the complete-character layer once per hour and does not add
     * minute interpolation to it. */
    const float hour_rotation = (float)(time->hour % 12) * tau / 12.0f;
    const float minute_rotation = (float)time->minute * tau / 60.0f;

    /* Preserve the APK scene order. The complete-character hour layer provides
     * the body, while the separate minute arm is a foreground limb. Drawing
     * the body after the arm hides its proximal section and makes it look cut
     * off even when the pivot coordinates are correct. */
    watch_draw_asset_layer(kkd2_background_start, kkd2_background_end, 0.0f);
    watch_draw_asset_layer(kkd2_complication_start, kkd2_complication_end, 0.0f);
    watch_draw_asset_layer(kkd2_hour_start, kkd2_hour_end, hour_rotation);
    watch_draw_asset_layer(kkd2_minute_start, kkd2_minute_end, minute_rotation);

    char digital[12];
    char date[12];
    snprintf(digital, sizeof(digital), "%02u:%02u",
             (unsigned)time->hour % 24U, (unsigned)time->minute % 60U);
    watch_text_optically_centered(digital,
                         WATCH_KKD2_COMPLICATION_CENTER_X,
                         WATCH_KKD2_TIME_TOP, &lv_font_montserrat_18,
                         rgb565(255, 246, 221));
    snprintf(date, sizeof(date), "%02d/%02d", time->month, time->day);
    watch_text_optically_centered(date,
                         WATCH_KKD2_COMPLICATION_CENTER_X,
                         WATCH_KKD2_DATE_TOP, &lv_font_montserrat_14,
                         rgb565(178, 162, 138));
    if (s_alarm_enabled) {
        char alarm[16];
        snprintf(alarm, sizeof(alarm), "A %02u:%02u", s_alarm_hour, s_alarm_minute);
        skyorb_text_centered(alarm, LCD_WIDTH / 2, 432, &lv_font_montserrat_14,
                             s_alarm_ringing ? rgb565(255, 69, 58) : rgb565(116, 45, 22));
    }
}

static int32_t watch_advance_display_time(int32_t displayed, int32_t target,
                                          bool *catch_up_pending)
{
    if (catch_up_pending != NULL) *catch_up_pending = false;
    if (target < 0 || target >= 24 * 60 * 60) return displayed;
    if (displayed < 0 || displayed >= 24 * 60 * 60) return target;

    int32_t delta = target - displayed;
    if (delta > 12 * 60 * 60) delta -= 24 * 60 * 60;
    else if (delta < -12 * 60 * 60) delta += 24 * 60 * 60;

    if (delta > 0 && delta <= 10) {
        displayed = (displayed + 1) % (24 * 60 * 60);
        if (catch_up_pending != NULL) *catch_up_pending = delta > 1;
        return displayed;
    }
    if (delta == -1) {
        /* RTC cache sampling may briefly trail a frame at a second boundary.
         * Never move the visible dial backwards for that transient. */
        return displayed;
    }
    return delta == 0 ? displayed : target;
}

static void watch_compose_kkd1(const clock_time_t *time)
{
    /* Every visible KKD1 dial element already comes from the APK layers.
     * Starting from a clean frame prevents the old procedural gold face from
     * appearing underneath those layers as a second, overlapping watch face. */
    memset(s_lcd_canvas, 0, LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t));
    const int center_x = LCD_WIDTH / 2;
    const uint16_t deep_gold = rgb565(124, 54, 20);

    watch_draw_time_dials(time);

    /* XML PartImage wfs_6: x=106, y=121, width=110, height=110.  Keep the
     * original ornate bezel instead of replacing it with a larger synthetic
     * panel, otherwise the upper-left centre looks like a second face. */
    watch_draw_asset_layer(kkd1_complication_start, kkd1_complication_end, 0.0f);
    char digital[12];
    const unsigned display_hour = (unsigned)time->hour % 24U;
    const unsigned display_minute = (unsigned)time->minute % 60U;
    snprintf(digital, sizeof(digital), "%02u:%02u", display_hour, display_minute);
    watch_text_optically_centered(digital,
                         WATCH_KKD1_COMPLICATION_CENTER_X,
                         WATCH_KKD1_TIME_TOP, &lv_font_montserrat_18,
                         rgb565(255, 255, 255));
    char date[12];
    snprintf(date, sizeof(date), "%02d/%02d", time->month, time->day);
    watch_text_optically_centered(date,
                         WATCH_KKD1_COMPLICATION_CENTER_X,
                         WATCH_KKD1_DATE_TOP, &lv_font_montserrat_14,
                         rgb565(173, 173, 173));

    watch_draw_character_layer();
    if (s_alarm_enabled) {
        char alarm[16];
        snprintf(alarm, sizeof(alarm), "A %02u:%02u", s_alarm_hour, s_alarm_minute);
        skyorb_text_centered(alarm, center_x, 422, &lv_font_montserrat_14,
                             s_alarm_ringing ? rgb565(255, 69, 58) : deep_gold);
    } else if (!time->valid) {
        skyorb_text_centered("RTC SET", center_x, 421, &lv_font_montserrat_14,
                             rgb565(255, 159, 10));
    }
}

static void watch_compose_canvas(const clock_time_t *time)
{
    if (s_watch_style == 1U) {
        watch_compose_kkd2(time);
    } else {
        watch_compose_kkd1(time);
    }
}

static void watch_render_frame(void)
{
    if (s_lcd_canvas == NULL) return;

    if (s_watch_rendered_style != s_watch_style) {
        /* Do not rely on the panel retaining or replacing an old face during
         * a style transition.  Flush an explicit black full frame first, then
         * compose exactly one requested APK face below.  This happens only
         * once per style change, so normal one-second animation is unaffected. */
        memset(s_lcd_canvas, 0, LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t));
        const dirty_rect_t clear = {
            .x1 = 0, .y1 = 0, .x2 = LCD_WIDTH, .y2 = LCD_HEIGHT
        };
        (void)flush_canvas_rect(&clear);
        s_watch_rendered_style = s_watch_style;
        ESP_LOGI(TAG, "Watch renderer switched to %s (%u); previous frame cleared",
                 s_watch_style == 1U ? "KKD2" : "KKD1",
                 (unsigned)s_watch_style);
    }

    const clock_time_t time = watch_read_time();
    const int32_t target_seconds = time.hour * 3600 + time.minute * 60 + time.second;
    bool catch_up_pending = false;
    s_watch_display_seconds = watch_advance_display_time(
        s_watch_display_seconds, target_seconds, &catch_up_pending);

    clock_time_t display_time = time;
    if (s_watch_display_seconds >= 0 && s_watch_display_seconds < 24 * 60 * 60) {
        display_time.hour = s_watch_display_seconds / 3600;
        display_time.minute = (s_watch_display_seconds / 60) % 60;
        display_time.second = s_watch_display_seconds % 60;
    }
    watch_compose_canvas(&display_time);
    const dirty_rect_t full = {.x1 = 0, .y1 = 0, .x2 = LCD_WIDTH, .y2 = LCD_HEIGHT};
    (void)flush_canvas_rect(&full);
    s_watch_dirty = catch_up_pending;
}

static void watch_enter(void)
{
    s_ui_screen = UI_SCREEN_WATCH;
    s_watch_dirty = true;
    s_watch_last_frame = 0;
    s_watch_display_seconds = -1;
    /* Re-entering the app is also a hard visual boundary. */
    s_watch_rendered_style = UINT8_MAX;
    s_apps_canvas_valid = false;
    s_apps_home_transition_frame = NULL;
    block_touch_until_release();
    ESP_LOGI(TAG, "Apps: entered mechanical Watch");
}

static void watch_handle_touch(int x, int y)
{
    if (s_alarm_ringing) {
        alarm_stop();
        block_touch_until_release();
        ESP_LOGI(TAG, "Watch: alarm dismissed by touch");
        return;
    }
    if (x < 104 && y < 104) {
        s_ui_screen = UI_SCREEN_HOME;
        s_home_dirty = true;
        block_touch_until_release();
        ESP_LOGI(TAG, "Watch: returned home");
        return;
    }
    /* Watch style is selected explicitly from the phone settings.  An ordinary
     * touch must not silently flip the persisted KKD1/KKD2 choice. */
    block_touch_until_release();
}

static void skyorb_start_network_task(void)
{
    s_skyorb_wifi_enabled = true;
    (void)pacon_save_switch("wifi_on", true);
    skyorb_mark_dirty();
    if (s_skyorb_mutex == NULL) {
        s_skyorb_mutex = xSemaphoreCreateMutex();
    }
    if (s_skyorb_mutex == NULL) {
        (void)skyorb_network_failure("network mutex", ESP_ERR_NO_MEM);
        return;
    }
    if (!s_skyorb_network_task_started && s_skyorb_mutex != NULL) {
        s_skyorb_network_task_started = true;
        BaseType_t started = xTaskCreate(skyorb_network_task, "skyorb_net", 8192, NULL, 4, NULL);
        if (started != pdPASS) {
            s_skyorb_network_task_started = false;
            (void)skyorb_network_failure("network task", ESP_ERR_NO_MEM);
        }
    }
}

static int settings_screen_y(int content_y)
{
    return content_y - s_settings_scroll_y;
}

static void settings_text(const char *text, int x, int content_y,
                          const lv_font_t *font, uint16_t colour)
{
    const int y = settings_screen_y(content_y);
    if (y + font->line_height >= 92 && y < 448) {
        const dirty_rect_t viewport = {.x1 = 54, .y1 = 92, .x2 = 421, .y2 = 448};
        skyorb_text_clipped(text, x, y, font, colour, &viewport);
    }
}

static void settings_switch(int y, bool enabled, uint16_t accent)
{
    const int screen_y = settings_screen_y(y);
    const dirty_rect_t clip = {.x1 = 54, .y1 = 92, .x2 = 421, .y2 = 448};
    fill_canvas_round_rect(358, screen_y, 407, screen_y + 30, 15,
                           enabled ? accent : rgb565(74, 74, 78), &clip);
    skyorb_circle_dot(enabled ? 392 : 373, screen_y + 15, 11,
                      rgb565(250, 250, 250));
}

static void device_settings_compose_canvas(bool full_refresh)
{
    const int center_x = LCD_WIDTH / 2;
    const int center_y = LCD_HEIGHT / 2;
    const uint16_t black = rgb565(0, 0, 0);
    const uint16_t panel = rgb565(28, 28, 30);
    const uint16_t blue = rgb565(10, 132, 255);
    const uint16_t green = rgb565(48, 209, 88);
    const uint16_t orange = rgb565(255, 159, 10);
    const uint16_t white = rgb565(245, 245, 247);
    const uint16_t secondary = rgb565(174, 174, 178);
    const dirty_rect_t viewport = {.x1 = 54, .y1 = 92, .x2 = 421, .y2 = 448};

    if (full_refresh) {
        for (int y = 0; y < LCD_HEIGHT; ++y) {
            uint16_t *line = s_lcd_canvas + (size_t)y * LCD_WIDTH;
            for (int x = 0; x < LCD_WIDTH; ++x) {
                const int dx = x - center_x;
                const int dy = y - center_y;
                line[x] = (dx * dx + dy * dy <= 230 * 230) ? black : 0;
            }
        }
    } else {
        /* During a scroll the fixed header and the circular border are
         * unchanged.  Clear and redraw only the card viewport instead of
         * rebuilding all 221k canvas pixels. */
        fill_canvas_rect(viewport.x1, viewport.y1, viewport.x2, viewport.y2,
                         black, NULL);
    }

    /* Scrollable watchOS-style cards. */
    const int cards[][2] = {{104, 126}, {246, 132}, {394, 126}, {536, 108}};
    for (size_t i = 0; i < sizeof(cards) / sizeof(cards[0]); ++i) {
        const int top = settings_screen_y(cards[i][0]);
        fill_canvas_rect(54, top, 421, top + cards[i][1], panel, &viewport);
    }

    skyorb_line(84, 62, 70, 76, white, 255);
    skyorb_line(70, 76, 84, 90, white, 255);
    skyorb_text("SETTINGS", 145, 48, &lv_font_montserrat_18, white);
    skyorb_text("BLE", 356, 48, &lv_font_montserrat_14,
                 ble_pacon_is_connected() ? green : secondary);

    skyorb_circle_dot(84, settings_screen_y(128), 16, blue);
    settings_text("CONNECTIVITY", 110, 112, &lv_font_montserrat_18, white);
    settings_text("Wi-Fi", 86, 151, &lv_font_montserrat_14, white);
    settings_text(s_skyorb_wifi_enabled ? "ON" : "OFF",
                  290, 155, &lv_font_montserrat_14,
                  s_skyorb_wifi_enabled ? green : secondary);
    settings_switch(147, s_skyorb_wifi_enabled, blue);
    settings_text("Bluetooth", 86, 204, &lv_font_montserrat_14, white);
    settings_text(ble_pacon_is_enabled() ? (ble_pacon_is_connected() ? "LINK" : "ON") : "OFF",
                  270, 208, &lv_font_montserrat_14,
                  ble_pacon_is_enabled() ? green : secondary);
    settings_switch(200, ble_pacon_is_enabled(), rgb565(90, 200, 250));
    settings_text("DISPLAY", 86, 262, &lv_font_montserrat_18, white);
    settings_text("Brightness", 86, 302, &lv_font_montserrat_14, white);
    char brightness[12];
    snprintf(brightness, sizeof(brightness), "%u%%",
             (unsigned)(((uint16_t)(s_user_brightness - SETTINGS_BRIGHTNESS_MIN) * 100U) /
                        (SETTINGS_BRIGHTNESS_MAX - SETTINGS_BRIGHTNESS_MIN)));
    settings_text(brightness, 350, 302, &lv_font_montserrat_14, orange);
    const int slider_y = settings_screen_y(340);
    fill_canvas_rect(92, slider_y, 382, slider_y + 8, rgb565(74, 74, 78), &viewport);
    const int knob_x = 92 + ((int)(s_user_brightness - SETTINGS_BRIGHTNESS_MIN) * 290) /
                              (SETTINGS_BRIGHTNESS_MAX - SETTINGS_BRIGHTNESS_MIN);
    fill_canvas_rect(92, slider_y, knob_x, slider_y + 8, orange, &viewport);
    skyorb_circle_dot(knob_x, slider_y + 4, 12, white);

    skyorb_circle_dot(84, settings_screen_y(420), 16, rgb565(110, 80, 220));
    settings_text("SKYORB", 110, 404, &lv_font_montserrat_18, white);
    settings_text("BLE app config; AP is transitional", 86, 444,
                  &lv_font_montserrat_14, secondary);
    settings_text(s_skyorb_config.location_valid ? "LOCATION READY" : "LOCATION NOT SET",
                  86, 476, &lv_font_montserrat_14,
                  s_skyorb_config.location_valid ? green : orange);

    settings_text("SYSTEM", 86, 548, &lv_font_montserrat_18, white);
    settings_text("USB media and battery status", 86, 589,
                  &lv_font_montserrat_14, secondary);
    settings_text(s_vbus_present ? "USB POWER" : "BATTERY POWER", 86, 618,
                  &lv_font_montserrat_14, s_vbus_present ? green : secondary);

    /* Repaint the fixed header so scrolled cards cannot cover it. */
    fill_canvas_rect(54, 20, 421, 94, black, NULL);
    skyorb_line(84, 62, 70, 76, white, 255);
    skyorb_line(70, 76, 84, 90, white, 255);
    skyorb_text("SETTINGS", 145, 48, &lv_font_montserrat_18, white);
    skyorb_text("BLE", 356, 48, &lv_font_montserrat_14,
                 ble_pacon_is_connected() ? green : secondary);
}

static void render_device_settings_frame(void)
{
    if (s_lcd_canvas == NULL) return;
    device_settings_compose_canvas(s_settings_full_refresh);
    /* The fixed header does not move while the cards scroll.  After the
     * first frame, transfer only the card viewport (367 x 356) and repaint
     * the header when its BLE status changes.  This avoids a full 475 x 475
     * QSPI transfer for every finger movement. */
    if (s_settings_full_refresh) {
        const dirty_rect_t full = {.x1 = 0, .y1 = 0, .x2 = LCD_WIDTH, .y2 = LCD_HEIGHT};
        (void)flush_canvas_rect(&full);
        s_settings_full_refresh = false;
        s_settings_header_dirty = false;
    } else {
        const dirty_rect_t content = {.x1 = 54, .y1 = 92, .x2 = 421, .y2 = 448};
        (void)flush_canvas_rect(&content);
        if (s_settings_header_dirty) {
            const dirty_rect_t header = {.x1 = 54, .y1 = 20, .x2 = 421, .y2 = 94};
            (void)flush_canvas_rect(&header);
            s_settings_header_dirty = false;
        }
    }
    s_device_settings_dirty = false;
}

static void wifi_settings_compose_canvas(void)
{
    const uint16_t black = rgb565(0, 0, 0);
    const uint16_t panel = rgb565(28, 28, 30);
    const uint16_t white = rgb565(245, 245, 247);
    const uint16_t secondary = rgb565(174, 174, 178);
    const uint16_t blue = rgb565(10, 132, 255);
    const uint16_t green = rgb565(48, 209, 88);
    const uint16_t orange = rgb565(255, 159, 10);
    const int center = LCD_WIDTH / 2;

    for (int y = 0; y < LCD_HEIGHT; ++y) {
        uint16_t *line = s_lcd_canvas + (size_t)y * LCD_WIDTH;
        for (int x = 0; x < LCD_WIDTH; ++x) {
            const int dx = x - center, dy = y - center;
            line[x] = dx * dx + dy * dy <= 230 * 230 ? black : 0;
        }
    }

    skyorb_line(84, 62, 70, 76, white, 255);
    skyorb_line(70, 76, 84, 90, white, 255);
    skyorb_text("WI-FI", 190, 48, &lv_font_montserrat_18, white);

    fill_canvas_rect(54, 104, 421, 169, panel, NULL);
    skyorb_text("Wi-Fi", 84, 123, &lv_font_montserrat_18, white);
    fill_canvas_round_rect(348, 120, 407, 153, 16,
                           s_skyorb_wifi_enabled ? green : rgb565(74, 74, 78), NULL);
    skyorb_circle_dot(s_skyorb_wifi_enabled ? 391 : 365, 136, 12, white);

    if (!s_skyorb_wifi_enabled) {
        skyorb_text("Wi-Fi is off", 169, 211, &lv_font_montserrat_18, secondary);
        skyorb_text("Turn it on to scan networks", 124, 245,
                    &lv_font_montserrat_14, secondary);
        return;
    }

    skyorb_text("SAVED NETWORKS", 70, 181, &lv_font_montserrat_14, secondary);
    skyorb_config_t config = {0};
    if (s_skyorb_mutex != NULL) {
        xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
        config = s_skyorb_config;
        xSemaphoreGive(s_skyorb_mutex);
    } else {
        config = s_skyorb_config;
    }
    if (config.wifi_count == 0) {
        fill_canvas_rect(54, 202, 421, 292, panel, NULL);
        skyorb_text("No saved networks", 84, 224, &lv_font_montserrat_18, white);
        skyorb_text("Add one with the PACON BLE app", 84, 258,
                    &lv_font_montserrat_14, secondary);
    } else {
        for (uint8_t index = 0; index < config.wifi_count; ++index) {
            const int row_y = 202 + index * 50;
            const bool selected = index == config.wifi_selected;
            fill_canvas_rect(54, row_y, 421, row_y + 45, panel, NULL);
            skyorb_circle_dot(72, row_y + 22, selected ? 5 : 3,
                              selected ? blue : rgb565(72, 72, 74));
            skyorb_text(config.wifi_profiles[index].ssid, 86, row_y + 5,
                        &lv_font_montserrat_14, white);
            const char *state = s_wifi_scan_in_progress ? "SCANNING" :
                                (s_wifi_profile_visible[index] ? "IN RANGE" : "SAVED");
            uint16_t state_colour = s_wifi_profile_visible[index] ? blue : secondary;
            if (selected && s_skyorb_wifi_connected) {
                state = "CONNECTED";
                state_colour = green;
            } else if (selected && s_wifi_should_connect) {
                static const char *dots[] = {"CONNECTING", "CONNECTING.",
                                             "CONNECTING..", "CONNECTING..."};
                state = s_wifi_link_state == WIFI_LINK_ASSOCIATED ? "GETTING IP..." :
                    dots[(xTaskGetTickCount() / pdMS_TO_TICKS(350)) & 3U];
                state_colour = orange;
            } else if (selected && s_wifi_link_state == WIFI_LINK_AUTH_FAILED) {
                state = "AUTH FAILED";
                state_colour = rgb565(255, 105, 97);
            } else if (selected && s_wifi_link_state == WIFI_LINK_TIMEOUT) {
                state = "TIMEOUT";
                state_colour = orange;
            }
            skyorb_text(state, 86, row_y + 25, &lv_font_montserrat_14, state_colour);
            if (s_wifi_delete_profile == index) {
                fill_canvas_rect(326, row_y + 7, 405, row_y + 38,
                                 rgb565(90, 24, 28), NULL);
                skyorb_text("DELETE", 338, row_y + 14, &lv_font_montserrat_14,
                            rgb565(255, 105, 97));
            } else if (s_wifi_profile_visible[index]) {
                char rssi[16];
                snprintf(rssi, sizeof(rssi), "%d dBm", (int)s_wifi_profile_rssi[index]);
                skyorb_text(rssi, 344, row_y + 14, &lv_font_montserrat_14, secondary);
            }
        }
    }
}

static void render_wifi_settings_frame(void)
{
    if (s_lcd_canvas == NULL) return;
    wifi_settings_compose_canvas();
    const dirty_rect_t full = {.x1 = 0, .y1 = 0, .x2 = LCD_WIDTH, .y2 = LCD_HEIGHT};
    (void)flush_canvas_rect(&full);
    s_wifi_settings_dirty = false;
    s_wifi_settings_last_frame = xTaskGetTickCount();
}

static void wifi_settings_enter(void)
{
    s_ui_screen = UI_SCREEN_WIFI_SETTINGS;
    s_wifi_settings_dirty = true;
    s_wifi_delete_profile = -1;
    s_wifi_touch_profile = -1;
    if (s_skyorb_wifi_enabled && !s_wifi_scan_in_progress) s_wifi_scan_requested = true;
    block_touch_until_release();
    ESP_LOGI(TAG, "Settings: entered Wi-Fi detail page");
}

static int wifi_settings_profile_at_y(int y, uint8_t count)
{
    if (y < 202) return -1;
    const int index = (y - 202) / 50;
    if (index < 0 || index >= count || y > 202 + index * 50 + 45) return -1;
    return index;
}

static void wifi_settings_select_profile(uint8_t index)
{
    (void)wifi_request_profile_connection(index);
}

static void wifi_settings_delete_profile(uint8_t index)
{
    skyorb_config_t config = {0};
    ble_snapshot_skyorb_config(&config);
    if (index >= config.wifi_count) return;
    const bool deleted_active = index == config.wifi_selected;
    char deleted_ssid[33];
    snprintf(deleted_ssid, sizeof(deleted_ssid), "%s", config.wifi_profiles[index].ssid);
    (void)wifi_config_delete_profile(&config, index);
    ble_store_skyorb_config(&config);
    if (deleted_active) {
        (void)pacon_save_switch("wifi_join", false);
        s_wifi_should_connect = false;
        s_wifi_connect_requested = false;
        if (s_skyorb_wifi_connected) (void)esp_wifi_disconnect();
        s_wifi_link_state = WIFI_LINK_IDLE;
    }
    memset(s_wifi_profile_visible, 0, sizeof(s_wifi_profile_visible));
    memset(s_wifi_profile_ap_valid, 0, sizeof(s_wifi_profile_ap_valid));
    s_wifi_delete_profile = -1;
    s_wifi_scan_requested = s_skyorb_wifi_enabled;
    s_wifi_settings_dirty = true;
    ESP_LOGI(TAG, "Wi-Fi page: deleted saved profile %u (%s)",
             (unsigned)index, deleted_ssid);
}

static void wifi_settings_handle_touch(int x, int y)
{
    if (x < 128 && y < 100) {
        settings_enter();
        return;
    }
    if (y >= 104 && y <= 174) {
        if (s_skyorb_wifi_enabled) {
            skyorb_disable_network();
        } else {
            skyorb_start_network_task();
            s_wifi_scan_requested = true;
        }
        s_wifi_settings_dirty = true;
        block_touch_until_release();
        return;
    }
    skyorb_config_t config = {0};
    ble_snapshot_skyorb_config(&config);
    const int profile = wifi_settings_profile_at_y(y, config.wifi_count);
    if (s_skyorb_wifi_enabled && profile >= 0) {
        if (s_wifi_delete_profile == profile && x >= 315) {
            wifi_settings_delete_profile((uint8_t)profile);
            block_touch_until_release();
            return;
        }
        s_wifi_touch_profile = profile;
        s_wifi_touch_started_us = esp_timer_get_time();
        s_wifi_touch_long_handled = false;
        if (s_wifi_delete_profile >= 0 && s_wifi_delete_profile != profile) {
            s_wifi_delete_profile = -1;
            s_wifi_settings_dirty = true;
        }
        return;
    }
    if (s_wifi_delete_profile >= 0) {
        s_wifi_delete_profile = -1;
        s_wifi_settings_dirty = true;
    }
}

static void wifi_settings_update_touch(int x, int y)
{
    (void)x;
    if (s_wifi_touch_profile < 0 || s_wifi_touch_long_handled) return;
    skyorb_config_t config = {0};
    ble_snapshot_skyorb_config(&config);
    if (wifi_settings_profile_at_y(y, config.wifi_count) != s_wifi_touch_profile) {
        s_wifi_touch_profile = -1;
        return;
    }
    if (esp_timer_get_time() - s_wifi_touch_started_us >= 700000LL) {
        s_wifi_delete_profile = s_wifi_touch_profile;
        s_wifi_touch_long_handled = true;
        s_wifi_settings_dirty = true;
        ESP_LOGI(TAG, "Wi-Fi page: delete action revealed for profile %d",
                 s_wifi_delete_profile);
    }
}

static void wifi_settings_handle_release(void)
{
    if (s_wifi_touch_profile >= 0 && !s_wifi_touch_long_handled) {
        wifi_settings_select_profile((uint8_t)s_wifi_touch_profile);
    }
    s_wifi_touch_profile = -1;
    s_wifi_touch_long_handled = false;
}

static void camera_enter(void)
{
    s_ui_screen = UI_SCREEN_CAMERA;
    s_camera_dirty = true;
    s_camera_feedback_until = 0;
    s_camera_last_frame = 0;
    s_apps_canvas_valid = false;
    s_apps_home_transition_frame = NULL;
    block_touch_until_release();
    ESP_LOGI(TAG, "Apps: entered Camera Remote");
}

static void camera_handle_touch(int x, int y)
{
    if (x < 120 && y < 105) {
        s_ui_screen = UI_SCREEN_APPS;
        s_apps_dirty = true;
        s_apps_canvas_valid = false;
        s_apps_home_transition_frame = NULL;
        block_touch_until_release();
        ESP_LOGI(TAG, "Camera: returned to app launcher");
        return;
    }
    const int dx = x - LCD_WIDTH / 2;
    const int dy = y - 255;
    if (dx * dx + dy * dy > 92 * 92) return;

    s_camera_feedback_result = ble_pacon_camera_shutter();
    s_camera_feedback_started = xTaskGetTickCount();
    s_camera_feedback_until = s_camera_feedback_started + pdMS_TO_TICKS(850);
    s_camera_dirty = true;
    block_touch_until_release();
    if (s_camera_feedback_result == ESP_OK) {
        ESP_LOGI(TAG, "Camera page: shutter sent");
    } else {
        ESP_LOGW(TAG, "Camera page: shutter unavailable (%s)",
                 esp_err_to_name(s_camera_feedback_result));
    }
}

static void camera_render_frame(void)
{
    if (s_lcd_canvas == NULL) return;
    const TickType_t now = xTaskGetTickCount();
    const bool ready = ble_pacon_is_enabled() &&
                       ble_pacon_is_camera_remote_ready();
    const bool feedback = s_camera_feedback_until != 0 &&
                          (int32_t)(s_camera_feedback_until - now) > 0;
    const bool success = feedback && s_camera_feedback_result == ESP_OK;
    const uint32_t age_ms = feedback ?
        (uint32_t)((now - s_camera_feedback_started) * portTICK_PERIOD_MS) : 0U;
    const bool pressed = feedback && age_ms < 130U;
    const bool flash = success && age_ms >= 130U && age_ms < 320U;
    const uint16_t black = rgb565(0, 0, 0);
    const uint16_t white = rgb565(245, 245, 247);
    const uint16_t secondary = rgb565(142, 142, 147);
    const uint16_t green = rgb565(48, 209, 88);
    const uint16_t red = rgb565(255, 69, 58);
    const uint16_t ring = success ? green :
        (feedback ? red : (ready ? green : rgb565(92, 92, 98)));

    for (int py = 0; py < LCD_HEIGHT; ++py) {
        uint16_t *line = s_lcd_canvas + (size_t)py * LCD_WIDTH;
        for (int px = 0; px < LCD_WIDTH; ++px) {
            const int dx = px - LCD_WIDTH / 2;
            const int dy = py - LCD_HEIGHT / 2;
            line[px] = dx * dx + dy * dy <= 230 * 230 ? black : 0;
        }
    }
    skyorb_line(84, 62, 70, 76, white, 255);
    skyorb_line(70, 76, 84, 90, white, 255);
    skyorb_text("CAMERA", 176, 48, &lv_font_montserrat_18, white);
    skyorb_text(ready ? "PHONE CONNECTED" : "PHONE NOT CONNECTED",
                ready ? 153 : 132, 105, &lv_font_montserrat_14,
                ready ? green : secondary);

    const int button_y = 255;
    const int radius = pressed ? 72 : 82;
    skyorb_circle_dot(LCD_WIDTH / 2, button_y, radius + 12, ring);
    skyorb_circle_dot(LCD_WIDTH / 2, button_y, radius + 5,
                      flash ? white : rgb565(24, 25, 31));
    skyorb_circle_dot(LCD_WIDTH / 2, button_y, radius,
                      flash ? rgb565(220, 255, 229) : rgb565(46, 48, 57));
    /* Camera glyph. */
    fill_canvas_round_rect(194, button_y - 24, 281, button_y + 31, 12,
                           flash ? rgb565(35, 45, 38) : white, NULL);
    fill_canvas_round_rect(215, button_y - 35, 260, button_y - 18, 7,
                           flash ? rgb565(35, 45, 38) : white, NULL);
    skyorb_circle_dot(LCD_WIDTH / 2, button_y + 3, 19,
                      flash ? white : rgb565(46, 48, 57));
    skyorb_circle_dot(LCD_WIDTH / 2, button_y + 3, 11,
                      flash ? green : rgb565(20, 22, 28));

    const char *message = feedback ?
        (success ? "CAPTURED" : "NOT CONNECTED") : "TAP TO CAPTURE";
    skyorb_text(message, success ? 181 : (feedback ? 153 : 164), 375,
                 &lv_font_montserrat_18,
                 feedback ? (success ? green : red) : white);
    skyorb_text("The ring shows the phone link", 124, 408,
                 &lv_font_montserrat_14, secondary);

    const dirty_rect_t full = {.x1 = 0, .y1 = 0, .x2 = LCD_WIDTH, .y2 = LCD_HEIGHT};
    (void)flush_canvas_rect(&full);
    s_camera_dirty = false;
    s_camera_last_frame = now;
}

static void mic_test_format_db(char *out, size_t out_size, int16_t dbfs_x10)
{
    if (dbfs_x10 <= -960) {
        snprintf(out, out_size, "--.-");
        return;
    }
    const int whole = dbfs_x10 / 10;
    const int decimal = abs(dbfs_x10 % 10);
    snprintf(out, out_size, "%d.%d", whole, decimal);
}

static void mic_test_enter(void)
{
    s_ui_screen = UI_SCREEN_MIC_TEST;
    s_mic_test_dirty = true;
    s_mic_test_last_frame = 0;
    s_mic_test_action_result = pacon_mic_open();
    s_apps_canvas_valid = false;
    s_apps_home_transition_frame = NULL;
    block_touch_until_release();
    if (s_mic_test_action_result == ESP_OK) {
        ESP_LOGI(TAG, "Mic test: monitoring; tap record for %u second WAV",
                 (unsigned)(PACON_MIC_MAX_RECORD_MS / 1000U));
    } else {
        ESP_LOGE(TAG, "Mic test: open failed (%s)",
                 esp_err_to_name(s_mic_test_action_result));
    }
}

static void mic_test_handle_touch(int x, int y)
{
    if (x < 120 && y < 105) {
        pacon_mic_close();
        s_ui_screen = UI_SCREEN_SETTINGS;
        s_settings_scroll_y = 0;
        s_settings_full_refresh = true;
        s_device_settings_dirty = true;
        block_touch_until_release();
        ESP_LOGI(TAG, "Mic test: returned to Settings and released I2S");
        return;
    }
    if (!home_in_round_rect(x, y, 76, 344, 399, 416, 28)) return;

    pacon_mic_status_t status;
    pacon_mic_get_status(&status);
    if (status.state == PACON_MIC_RECORDING) {
        s_mic_test_action_result = pacon_mic_stop_recording();
    } else if (status.state == PACON_MIC_MONITORING && s_sd_nand_mounted &&
               !s_usb_msc_started) {
        s_mic_test_action_result = pacon_mic_start_recording(MIC_TEST_WAV_PATH);
    } else {
        s_mic_test_action_result = status.last_error != ESP_OK ?
                                   status.last_error : ESP_ERR_INVALID_STATE;
    }
    s_mic_test_dirty = true;
    block_touch_until_release();
}

static void mic_test_render_frame(void)
{
    if (s_lcd_canvas == NULL) return;
    pacon_mic_status_t status;
    pacon_mic_get_status(&status);
    const uint16_t black = rgb565(0, 0, 0);
    const uint16_t panel = rgb565(28, 28, 30);
    const uint16_t white = rgb565(245, 245, 247);
    const uint16_t secondary = rgb565(142, 142, 147);
    const uint16_t green = rgb565(48, 209, 88);
    const uint16_t orange = rgb565(255, 159, 10);
    const uint16_t red = rgb565(255, 69, 58);
    const uint16_t cyan = rgb565(90, 200, 250);
    const int center_x = LCD_WIDTH / 2;
    const int center_y = LCD_HEIGHT / 2;

    for (int py = 0; py < LCD_HEIGHT; ++py) {
        uint16_t *line = s_lcd_canvas + (size_t)py * LCD_WIDTH;
        for (int px = 0; px < LCD_WIDTH; ++px) {
            const int dx = px - center_x;
            const int dy = py - center_y;
            line[px] = dx * dx + dy * dy <= 230 * 230 ? black : 0;
        }
    }
    skyorb_line(84, 62, 70, 76, white, 255);
    skyorb_line(70, 76, 84, 90, white, 255);
    skyorb_text("MIC TEST", 177, 48, &lv_font_montserrat_18, white);

    const char *state_text = "OFF";
    uint16_t state_colour = secondary;
    if (status.state == PACON_MIC_RECORDING) {
        state_text = "RECORDING";
        state_colour = red;
    } else if (status.state == PACON_MIC_MONITORING) {
        state_text = status.signal_present ? "LIVE SIGNAL" : "LISTENING";
        state_colour = status.signal_present ? green : orange;
    } else if (status.state == PACON_MIC_ERROR || s_mic_test_action_result != ESP_OK) {
        state_text = "AUDIO ERROR";
        state_colour = red;
    }
    skyorb_text_centered(state_text, center_x, 91, &lv_font_montserrat_14,
                         state_colour);

    fill_canvas_round_rect(54, 120, 421, 238, 18, panel, NULL);
    int32_t wave_peak = 1;
    for (size_t i = 0; i < PACON_MIC_WAVEFORM_POINTS; ++i) {
        int32_t magnitude = status.waveform[i] < 0 ?
                            -(int32_t)status.waveform[i] : status.waveform[i];
        if (magnitude > wave_peak) wave_peak = magnitude;
    }
    int previous_x = 68;
    int previous_y = 179 - (int)((int32_t)status.waveform[0] * 48 / wave_peak);
    for (size_t i = 1; i < PACON_MIC_WAVEFORM_POINTS; ++i) {
        const int x = 68 + (int)(i * 338U / (PACON_MIC_WAVEFORM_POINTS - 1U));
        const int y = 179 - (int)((int32_t)status.waveform[i] * 48 / wave_peak);
        skyorb_line(previous_x, previous_y, x, y, cyan, 255);
        previous_x = x;
        previous_y = y;
    }
    skyorb_line(68, 179, 406, 179, rgb565(72, 72, 74), 120);

    char rms[20], peak[20], floor_text[20];
    mic_test_format_db(rms, sizeof(rms), status.rms_dbfs_x10);
    mic_test_format_db(peak, sizeof(peak), status.peak_dbfs_x10);
    mic_test_format_db(floor_text, sizeof(floor_text), status.floor_dbfs_x10);
    char metrics[64];
    snprintf(metrics, sizeof(metrics), "RMS %s  PEAK %s dBFS", rms, peak);
    skyorb_text_centered(metrics, center_x, 252, &lv_font_montserrat_14, white);
    snprintf(metrics, sizeof(metrics), "QUIET FLOOR %s  CLIP %lu", floor_text,
             (unsigned long)status.clipped_samples);
    skyorb_text_centered(metrics, center_x, 277, &lv_font_montserrat_14, secondary);

    fill_canvas_round_rect(76, 307, 399, 327, 10, rgb565(58, 58, 60), NULL);
    const int level = clamp_int((status.peak_dbfs_x10 + 600) * 323 / 600, 0, 323);
    if (level > 0) {
        const uint16_t level_colour = status.peak_dbfs_x10 > -30 ? red :
                                      (status.peak_dbfs_x10 > -120 ? orange : green);
        fill_canvas_round_rect(76, 307, 76 + level, 327, 10, level_colour, NULL);
    }

    const bool recording = status.state == PACON_MIC_RECORDING;
    const bool can_record = status.state == PACON_MIC_MONITORING &&
                            s_sd_nand_mounted && !s_usb_msc_started;
    fill_canvas_round_rect(76, 344, 399, 416, 28,
                           recording ? rgb565(88, 24, 28) :
                           (can_record ? rgb565(20, 84, 54) : panel), NULL);
    char action[48];
    if (recording) {
        snprintf(action, sizeof(action), "STOP & SAVE  %lu.%lus",
                 (unsigned long)(status.recorded_ms / 1000U),
                 (unsigned long)((status.recorded_ms / 100U) % 10U));
    } else if (status.file_ready) {
        snprintf(action, sizeof(action), "SAVED - RECORD AGAIN");
    } else if (!s_sd_nand_mounted) {
        snprintf(action, sizeof(action), "SD NAND NOT READY");
    } else {
        snprintf(action, sizeof(action), "RECORD %u SECONDS",
                 (unsigned)(PACON_MIC_MAX_RECORD_MS / 1000U));
    }
    skyorb_text_centered(action, center_x, 368, &lv_font_montserrat_18,
                         recording ? red : (can_record ? green : secondary));
    skyorb_text_centered("/MIC_TEST.WAV - export with USB Disk", center_x, 425,
                         &lv_font_montserrat_14, secondary);

    const dirty_rect_t full = {.x1 = 0, .y1 = 0, .x2 = LCD_WIDTH, .y2 = LCD_HEIGHT};
    (void)flush_canvas_rect(&full);
    s_mic_test_dirty = false;
    s_mic_test_last_frame = xTaskGetTickCount();
}
static void settings_enter(void)
{
    s_ui_screen = UI_SCREEN_SETTINGS;
    s_settings_scroll_y = 0;
    s_settings_touch_dragging = false;
    s_settings_full_refresh = true;
    s_settings_header_dirty = false;
    s_settings_debug_taps = 0;
    s_settings_debug_deadline = 0;
    s_device_settings_dirty = true;
    s_apps_canvas_valid = false;
    s_apps_home_transition_frame = NULL;
    block_touch_until_release();
    ESP_LOGI(TAG, "Apps: entered vertical Settings; AP is not started automatically");
}

static void settings_handle_touch(int x, int y)
{
    if (x >= 128 && y < 100) {
        const TickType_t now = xTaskGetTickCount();
        if (s_settings_debug_deadline == 0 ||
            (int32_t)(now - s_settings_debug_deadline) >= 0) {
            s_settings_debug_taps = 0;
        }
        s_settings_debug_deadline = now + pdMS_TO_TICKS(1200);
        if (++s_settings_debug_taps >= 3U) {
            s_settings_debug_taps = 0;
            mic_test_enter();
        } else {
            block_touch_until_release();
        }
        return;
    }
    if (x < 128 && y < 100) {
        s_ui_screen = UI_SCREEN_APPS;
        s_apps_dirty = true;
        s_apps_canvas_valid = false;
        s_apps_home_transition_frame = NULL;
        block_touch_until_release();
        ESP_LOGI(TAG, "Settings: returned to app launcher");
        return;
    }
    const int content_y = y + s_settings_scroll_y;
    if (content_y >= 136 && content_y <= 188 && !s_settings_touch_dragging) {
        if (x < 330) {
            wifi_settings_enter();
            return;
        }
        const TickType_t now = xTaskGetTickCount();
        if (s_settings_wifi_last_toggle != 0 &&
            (int32_t)(now - s_settings_wifi_last_toggle) < pdMS_TO_TICKS(700)) {
            ESP_LOGW(TAG, "[WIFI-DBG] Settings Wi-Fi duplicate touch ignored");
            block_touch_until_release();
            return;
        }
        s_settings_wifi_last_toggle = now;
        const bool enable = !s_skyorb_wifi_enabled;
        if (enable) {
            ESP_LOGI(TAG, "[WIFI-DBG] Settings Wi-Fi ON requested; queueing dedicated network task");
            skyorb_start_network_task();
        } else {
            ESP_LOGI(TAG, "[WIFI-DBG] Settings Wi-Fi OFF requested");
            skyorb_disable_network();
        }
        s_device_settings_dirty = true;
        block_touch_until_release();
        return;
    }
    if (content_y >= 190 && content_y <= 234 && !s_settings_touch_dragging) {
        (void)ble_pacon_set_enabled(!ble_pacon_is_enabled());
        s_settings_header_dirty = true;
        s_device_settings_dirty = true;
        ESP_LOGI(TAG, "Settings: Bluetooth %s", ble_pacon_is_enabled() ? "enabled" : "disabled");
        return;
    }
    if (content_y >= 314 && content_y <= 374 && !s_settings_touch_dragging) {
        const int slider = clamp_int(x, 92, 382);
        s_user_brightness = (uint8_t)(SETTINGS_BRIGHTNESS_MIN +
            ((slider - 92) * (SETTINGS_BRIGHTNESS_MAX - SETTINGS_BRIGHTNESS_MIN)) / 290);
        (void)lcd_set_brightness(s_user_brightness);
        s_display_dimmed = false;
        settings_save_brightness();
        s_device_settings_dirty = true;
        return;
    }
}

static void skyorb_enter(void)
{
    s_ui_screen = UI_SCREEN_SKYORB;
    s_skyorb_dirty = true;
    s_skyorb_last_frame = 0;
    s_skyorb_rotate_active = false;
    s_skyorb_rotate_accumulated = 0.0f;
    s_apps_canvas_valid = false;
    s_apps_home_transition_frame = NULL;
    block_touch_until_release();
    if (s_skyorb_wifi_enabled) {
        skyorb_start_network_task();
        ESP_LOGI(TAG, "Apps: entered Sky Radar; Wi-Fi remains enabled");
    } else {
        ESP_LOGI(TAG, "Apps: entered Sky Radar in offline/demo mode; Wi-Fi remains disabled");
    }
}

static void skyorb_handle_touch(int x, int y)
{
    /* The top-left corner is deliberately invisible so the radar can occupy
     * the complete round face without a floating navigation control. */
    if (x < 104 && y < 104) {
        s_ui_screen = UI_SCREEN_HOME;
        s_home_dirty = true;
        block_touch_until_release();
        ESP_LOGI(TAG, "SkyOrb: returned home");
        return;
    }
    const float dx = (float)x - (float)LCD_WIDTH * 0.5f;
    const float dy = (float)y - (float)LCD_HEIGHT * 0.5f;
    s_skyorb_rotate_active = dx * dx + dy * dy >= 4900.0f;
    s_skyorb_rotate_last_angle = atan2f(dy, dx);
    s_skyorb_rotate_accumulated = 0.0f;
}

static void skyorb_handle_touch_move(int x, int y)
{
    if (!s_skyorb_rotate_active) return;
    const float dx = (float)x - (float)LCD_WIDTH * 0.5f;
    const float dy = (float)y - (float)LCD_HEIGHT * 0.5f;
    if (dx * dx + dy * dy < 3600.0f) return;
    const float angle = atan2f(dy, dx);
    float delta = angle - s_skyorb_rotate_last_angle;
    if (delta > 3.14159265f) delta -= 6.28318531f;
    if (delta < -3.14159265f) delta += 6.28318531f;
    s_skyorb_rotate_accumulated += delta;
    s_skyorb_rotate_last_angle = angle;
}

static void skyorb_handle_touch_release(void)
{
    if (!s_skyorb_rotate_active) return;
    s_skyorb_rotate_active = false;
    if (fabsf(s_skyorb_rotate_accumulated) < 0.40f) return;

    /* Screen-space positive rotation is clockwise. */
    if (s_skyorb_rotate_accumulated > 0.0f) {
        s_skyorb_range_index = (uint8_t)((s_skyorb_range_index + 1U) %
                                          SKYORB_RANGE_COUNT);
    } else {
        s_skyorb_range_index = (uint8_t)((s_skyorb_range_index +
                                          SKYORB_RANGE_COUNT - 1U) %
                                          SKYORB_RANGE_COUNT);
    }
    if (s_skyorb_mutex != NULL) {
        xSemaphoreTake(s_skyorb_mutex, portMAX_DELAY);
        s_skyorb_config.range_index = s_skyorb_range_index;
        xSemaphoreGive(s_skyorb_mutex);
    }
    skyorb_save_range();
    skyorb_mark_dirty();
    ESP_LOGI(TAG, "SkyOrb: %s rotation set range index %u",
             s_skyorb_rotate_accumulated > 0.0f ? "clockwise" : "counter-clockwise",
             (unsigned)s_skyorb_range_index);
}

static void render_home_frame(void)
{
    if (s_lcd_panel == NULL || s_lcd_stripe == NULL ||
        s_lcd_stripe_secondary == NULL || s_lcd_done == NULL) {
        return;
    }

    const int64_t render_start_us = esp_timer_get_time();
    uint64_t compose_us = 0;
    uint64_t dma_us = 0;
    uint8_t queued = 0;
    uint8_t stripe_index = 0;
    int64_t transfer_start_us = 0;
    uint16_t *const stripe_buffers[2] = {s_lcd_stripe, s_lcd_stripe_secondary};
    prepare_home_labels();
    const uint16_t *cached_frame = home_cached_frame(s_home_media_index,
                                                      s_home_animation_frame);
    for (int stripe_y = 0; stripe_y < LCD_HEIGHT; stripe_y += LCD_STRIPE_LINES) {
        /* Do not reuse either DMA buffer until its preceding transfer has
         * completed.  While QSPI sends buffer N, CPU composes buffer N + 1. */
        if (queued == 2) {
            if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
                ESP_LOGE(TAG, "Home pipelined DMA completion timed out");
                return;
            }
            --queued;
        }
        int stripe_end = clamp_int(stripe_y + LCD_STRIPE_LINES, 0, LCD_HEIGHT);
        const int64_t compose_start_us = esp_timer_get_time();
        uint16_t *stripe = stripe_buffers[stripe_index & 1U];
        for (int y = stripe_y; y < stripe_end; ++y) {
            uint16_t *line = stripe + (y - stripe_y) * LCD_WIDTH;
            if (cached_frame != NULL) {
                home_compose_cached_line(line, cached_frame, y);
            } else {
                for (int x = 0; x < LCD_WIDTH; ++x) {
                    line[x] = rgb565_for_sh8601(home_pixel(x, y));
                }
            }
        }
        compose_us += (uint64_t)(esp_timer_get_time() - compose_start_us);

        if (transfer_start_us == 0) {
            transfer_start_us = esp_timer_get_time();
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_lcd_panel, 0, stripe_y,
                                                   LCD_WIDTH, stripe_end, stripe);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Home stripe transfer failed: %s", esp_err_to_name(err));
            return;
        }
        ++queued;
        ++stripe_index;
    }
    while (queued > 0) {
        if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
            ESP_LOGE(TAG, "Home stripe DMA completion timed out");
            return;
        }
        --queued;
    }
    if (transfer_start_us != 0) {
        dma_us = (uint64_t)(esp_timer_get_time() - transfer_start_us);
    }
    s_home_dirty = false;
    home_record_render_time(render_start_us, compose_us, dma_us, "home");
}

/* SH8601 is only reliable when each transfer covers complete scan lines.
 * Compose the horizontal page movement in software, then send those stable
 * full-width stripes.  Four steps give the swipe a clear direction without
 * delaying the launcher gesture for too long. */
static void render_home_slide_frame(void)
{
    if (s_lcd_panel == NULL || s_lcd_stripe == NULL ||
        s_lcd_stripe_secondary == NULL || s_lcd_done == NULL) {
        return;
    }

    const int64_t render_start_us = esp_timer_get_time();
    uint64_t compose_us = 0;
    uint64_t dma_us = 0;
    uint8_t queued = 0;
    uint8_t stripe_index = 0;
    int64_t transfer_start_us = 0;
    uint16_t *const stripe_buffers[2] = {s_lcd_stripe, s_lcd_stripe_secondary};
    prepare_home_labels();
    const uint16_t *from_frame = home_cached_frame(s_home_slide_from_index,
                                                    s_home_slide_from_frame);
    const uint16_t *to_frame = home_cached_frame(s_home_media_index,
                                                  s_home_animation_frame);
    const int shift = ((int)s_home_slide_step * LCD_WIDTH) / HOME_SLIDE_STEPS;
    for (int stripe_y = 0; stripe_y < LCD_HEIGHT; stripe_y += LCD_STRIPE_LINES) {
        if (queued == 2) {
            if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
                ESP_LOGE(TAG, "Home slide pipelined DMA completion timed out");
                return;
            }
            --queued;
        }
        const int stripe_end = clamp_int(stripe_y + LCD_STRIPE_LINES, 0, LCD_HEIGHT);
        const int64_t compose_start_us = esp_timer_get_time();
        uint16_t *stripe = stripe_buffers[stripe_index & 1U];
        for (int y = stripe_y; y < stripe_end; ++y) {
            uint16_t *line = stripe + (size_t)(y - stripe_y) * LCD_WIDTH;
            if (from_frame != NULL && to_frame != NULL) {
                home_compose_cached_slide_line(line, from_frame, to_frame, y, shift,
                                               s_home_slide_direction);
            } else {
                for (int x = 0; x < LCD_WIDTH; ++x) {
                    line[x] = rgb565_for_sh8601(home_slide_pixel(x, y));
                }
            }
        }
        compose_us += (uint64_t)(esp_timer_get_time() - compose_start_us);
        if (transfer_start_us == 0) {
            transfer_start_us = esp_timer_get_time();
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_lcd_panel, 0, stripe_y,
                                                   LCD_WIDTH, stripe_end, stripe);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Home slide transfer failed: %s", esp_err_to_name(err));
            return;
        }
        ++queued;
        ++stripe_index;
    }
    while (queued > 0) {
        if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
            ESP_LOGE(TAG, "Home slide DMA completion timed out");
            return;
        }
        --queued;
    }
    if (transfer_start_us != 0) {
        dma_us = (uint64_t)(esp_timer_get_time() - transfer_start_us);
    }

    if (s_home_slide_step < HOME_SLIDE_STEPS) {
        ++s_home_slide_step;
    } else {
        s_home_slide_active = false;
        s_home_dirty = false;
    }
    home_record_render_time(render_start_us, compose_us, dma_us, "slide");
}

/* Compose a page transition a line at a time.  The old pixel-by-pixel path
 * called home_pixel_from_background() for every pixel in the incoming home
 * page, even though the cached home compositor only needs the status rows.
 * Keeping the two source pages as complete scan lines removes most of the
 * CPU-side stalls while preserving the SH8601 full-width transfer rule. */
static void apps_compose_transition_line(uint16_t *line, int y, bool entrance)
{
    const uint8_t step = entrance ? s_apps_entrance_step : s_apps_dismiss_step;
    const uint8_t steps = entrance ? APPS_ENTRANCE_STEPS : APPS_DISMISS_STEPS;
    const int shift = ((int)step * LCD_HEIGHT) / steps;
    const int source_y = entrance ? y + LCD_HEIGHT - shift : y + shift;
    if (source_y < LCD_HEIGHT) {
        for (int x = 0; x < LCD_WIDTH; ++x) {
            line[x] = rgb565_for_sh8601(apps_cached_pixel(x, source_y));
        }
        return;
    }

    const int home_y = source_y - LCD_HEIGHT;
    if (s_apps_home_transition_frame != NULL) {
        home_compose_cached_line(line, s_apps_home_transition_frame, home_y);
    } else {
        for (int x = 0; x < LCD_WIDTH; ++x) {
            line[x] = rgb565_for_sh8601(home_pixel(x, home_y));
        }
    }
}

static void render_apps_frame(void)
{
    if (s_lcd_panel == NULL || s_lcd_stripe == NULL ||
        s_lcd_stripe_secondary == NULL || s_lcd_done == NULL ||
        s_lcd_canvas == NULL) {
        return;
    }

    /* Compose the launcher once into PSRAM.  The old implementation ran
     * apps_pixel() for all 221k pixels on every frame, even though the icon
     * page is static.  This one-time cost is paid when the page first opens;
     * subsequent frames, including the return animation, only read the
     * cached native RGB565 pixels. */
    if (!s_apps_canvas_valid) {
        for (int y = 0; y < LCD_HEIGHT; ++y) {
            uint16_t *line = s_lcd_canvas + (size_t)y * LCD_WIDTH;
            for (int x = 0; x < LCD_WIDTH; ++x) {
                line[x] = apps_pixel(x, y);
            }
        }
        s_apps_canvas_valid = true;
    }
    if (s_apps_dismiss_active || s_apps_entrance_active) {
        prepare_home_labels();
        s_apps_home_transition_frame = home_cached_frame_for_index(s_home_media_index);
    }
    /* Queue two full-width stripes so CPU composition overlaps the previous
     * QSPI transfer.  The old one-buffer path waited after every stripe,
     * turning the five-step upward menu animation into visible pauses. */
    uint8_t queued = 0;
    uint8_t stripe_index = 0;
    uint16_t *const stripe_buffers[2] = {s_lcd_stripe, s_lcd_stripe_secondary};
    for (int stripe_y = 0; stripe_y < LCD_HEIGHT; stripe_y += LCD_STRIPE_LINES) {
        if (queued == 2) {
            if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
                ESP_LOGE(TAG, "Apps pipelined DMA completion timed out");
                return;
            }
            --queued;
        }
        const int stripe_end = clamp_int(stripe_y + LCD_STRIPE_LINES, 0, LCD_HEIGHT);
        uint16_t *stripe = stripe_buffers[stripe_index & 1U];
        for (int y = stripe_y; y < stripe_end; ++y) {
            uint16_t *line = stripe + (size_t)(y - stripe_y) * LCD_WIDTH;
            if (s_apps_dismiss_active || s_apps_entrance_active) {
                apps_compose_transition_line(line, y, s_apps_entrance_active);
            } else {
                for (int x = 0; x < LCD_WIDTH; ++x) {
                    line[x] = rgb565_for_sh8601(apps_cached_pixel(x, y));
                }
            }
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_lcd_panel, 0, stripe_y,
                                                   LCD_WIDTH, stripe_end, stripe);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Apps stripe transfer failed: %s", esp_err_to_name(err));
            return;
        }
        ++queued;
        ++stripe_index;
    }
    while (queued > 0) {
        if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
            ESP_LOGE(TAG, "Apps DMA completion timed out");
            return;
        }
        --queued;
    }
    if (s_apps_entrance_active) {
        if (s_apps_entrance_step < APPS_ENTRANCE_STEPS) {
            ++s_apps_entrance_step;
            s_apps_dirty = true;
        } else {
            s_apps_entrance_active = false;
            s_apps_entrance_step = 0;
            s_apps_dirty = false;
            s_apps_home_transition_frame = NULL;
            block_touch_until_release();
            ESP_LOGI(TAG, "Apps: downward entrance animation complete");
        }
    } else if (s_apps_dismiss_active) {
        if (s_apps_dismiss_step < APPS_DISMISS_STEPS) {
            ++s_apps_dismiss_step;
            s_apps_dirty = true;
        } else {
            s_apps_dismiss_active = false;
            s_apps_dismiss_step = 0;
            s_ui_screen = UI_SCREEN_HOME;
            s_home_dirty = true;
            s_apps_dirty = false;
            s_apps_home_transition_frame = NULL;
            block_touch_until_release();
            ESP_LOGI(TAG, "Apps: upward return animation complete");
        }
    } else {
        s_apps_dirty = false;
    }
}

static void render_usb_disk_frame(void)
{
    if (s_lcd_panel == NULL || s_lcd_stripe == NULL || s_lcd_done == NULL) {
        return;
    }
    for (int stripe_y = 0; stripe_y < LCD_HEIGHT; stripe_y += LCD_STRIPE_LINES) {
        const int stripe_end = clamp_int(stripe_y + LCD_STRIPE_LINES, 0, LCD_HEIGHT);
        for (int y = stripe_y; y < stripe_end; ++y) {
            uint16_t *line = s_lcd_stripe + (size_t)(y - stripe_y) * LCD_WIDTH;
            for (int x = 0; x < LCD_WIDTH; ++x) {
                line[x] = rgb565_for_sh8601(usb_disk_pixel(x, y));
            }
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_lcd_panel, 0, stripe_y,
                                                   LCD_WIDTH, stripe_end, s_lcd_stripe);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "USB disk screen transfer failed: %s", esp_err_to_name(err));
            return;
        }
        if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
            ESP_LOGE(TAG, "USB disk screen DMA completion timed out");
            return;
        }
    }
    s_usb_disk_dirty = false;
}

/* Both controls pages use the existing native canvas + double DMA stripes.
 * Keep full-width bands for the panel's verified QSPI addressing behaviour. */
static void fluid_controls_render_band(int y1, int y2, bool picker)
{
    for (int y = y1; y < y2; ++y) {
        for (int x = 0; x < LCD_WIDTH; ++x) {
            s_lcd_canvas[(size_t)y * LCD_WIDTH + x] = picker ?
                fluid_colour_picker_pixel(x, y) : fluid_settings_pixel(x, y);
        }
    }
}

static size_t s_fluid_controls_transfer_pixels;

static bool fluid_controls_flush_band(int y1, int y2)
{
    const dirty_rect_t rect = {.x1 = 0, .y1 = y1, .x2 = LCD_WIDTH, .y2 = y2};
    s_fluid_controls_transfer_pixels += (size_t)LCD_WIDTH * (y2 - y1);
    return flush_canvas_rect(&rect);
}

static void fluid_colour_restore_marker(int cx, int cy)
{
    for (int y = cy - 6; y <= cy + 6; ++y) {
        for (int x = cx - 6; x <= cx + 6; ++x) {
            s_lcd_canvas[(size_t)y * LCD_WIDTH + x] = fluid_colour_picker_pixel(x, y);
        }
    }
}

static void fluid_colour_draw_marker(int cx, int cy)
{
    for (int y = cy - 6; y <= cy + 6; ++y) {
        for (int x = cx - 6; x <= cx + 6; ++x) {
            const int dx = x - cx;
            const int dy = y - cy;
            const int d2 = dx * dx + dy * dy;
            /* Match the original wheel clip, including the outermost pixel. */
            const int wx = x - LCD_WIDTH / 2;
            const int wy = y - 270;
            if (d2 <= 36 && wx * wx + wy * wy <= 150 * 150) {
                s_lcd_canvas[(size_t)y * LCD_WIDTH + x] = d2 >= 16 ?
                    rgb565(246, 252, 255) : rgb565(2, 9, 20);
            }
        }
    }
}

static void fluid_controls_record_frame(int64_t started, int64_t drawn)
{
    const int64_t done = esp_timer_get_time();
    s_fluid_ui_perf.draw_us += drawn - started;
    s_fluid_ui_perf.transfer_us += done - drawn;
    s_fluid_ui_perf.pixels += s_fluid_controls_transfer_pixels;
    ++s_fluid_ui_perf.frames;
    if (s_fluid_controls_input_us != 0) {
        const uint32_t elapsed = (uint32_t)(done - s_fluid_controls_input_us);
        s_fluid_ui_perf.feedback_us += elapsed;
        ++s_fluid_ui_perf.feedbacks;
        if (elapsed > s_fluid_ui_perf.max_feedback_us)
            s_fluid_ui_perf.max_feedback_us = elapsed;
        s_fluid_controls_input_us = 0;
    }
}

static void fluid_controls_report_perf(void)
{
    const int64_t now = esp_timer_get_time();
    if (now < s_fluid_ui_perf.report_us) return;
    if (s_fluid_ui_perf.frames || s_fluid_ui_perf.saves) {
        const double frames = s_fluid_ui_perf.frames ? s_fluid_ui_perf.frames : 1;
        const double feedbacks = s_fluid_ui_perf.feedbacks ? s_fluid_ui_perf.feedbacks : 1;
        const double saves = s_fluid_ui_perf.saves ? s_fluid_ui_perf.saves : 1;
        ESP_LOGI(TAG, "[FLUID-UI-PERF] frames=%lu draw=%.2fms transfer=%.2fms pixels=%.0f "
                 "feedback=%.2fms max=%.2fms saves=%lu save=%.2fms pending=%d",
                 (unsigned long)s_fluid_ui_perf.frames,
                 s_fluid_ui_perf.draw_us / frames / 1000.0,
                 s_fluid_ui_perf.transfer_us / frames / 1000.0,
                 s_fluid_ui_perf.pixels / frames,
                 s_fluid_ui_perf.feedback_us / feedbacks / 1000.0,
                 s_fluid_ui_perf.max_feedback_us / 1000.0,
                 (unsigned long)s_fluid_ui_perf.saves,
                 s_fluid_ui_perf.save_us / saves / 1000.0,
                 s_fluid_preferences_pending);
        memset(&s_fluid_ui_perf, 0, sizeof(s_fluid_ui_perf));
    }
    s_fluid_ui_perf.report_us = now + 2000000;
}

static void render_fluid_settings_frame(void)
{
    if (s_lcd_canvas == NULL) return;
    const int64_t started = esp_timer_get_time();
    const bool full = s_fluid_controls_canvas_screen != UI_SCREEN_FLUID_SETTINGS;
    const int old_shape = s_fluid_settings_drawn_shape;
    const int new_shape = (int)s_fluid_shape;
    s_fluid_controls_transfer_pixels = 0;
    if (full) {
        fluid_controls_render_band(0, LCD_HEIGHT, false);
    } else if (old_shape != new_shape) {
        fluid_controls_render_band(112 + old_shape * 70, 172 + old_shape * 70, false);
        fluid_controls_render_band(112 + new_shape * 70, 172 + new_shape * 70, false);
    }
    const int64_t drawn = esp_timer_get_time();
    bool ok = true;
    if (full) {
        ok = fluid_controls_flush_band(0, LCD_HEIGHT);
    } else if (old_shape != new_shape) {
        ok = fluid_controls_flush_band(112 + old_shape * 70, 172 + old_shape * 70);
        if (ok) ok = fluid_controls_flush_band(112 + new_shape * 70, 172 + new_shape * 70);
    }
    if (!ok) {
        s_fluid_controls_canvas_screen = UI_SCREEN_HOME;
        return;
    }
    s_fluid_settings_drawn_shape = new_shape;
    s_fluid_controls_canvas_screen = UI_SCREEN_FLUID_SETTINGS;
    s_settings_dirty = false;
    fluid_controls_record_frame(started, drawn);
}

static void render_fluid_colour_picker_frame(void)
{
    if (s_lcd_canvas == NULL) return;
    const int64_t started = esp_timer_get_time();
    fluid_colour_prepare_cache();
    const bool full = s_fluid_controls_canvas_screen != UI_SCREEN_COLOUR_PICKER;
    const float angle = s_colour_hue * 6.283185307f;
    const int cx = LCD_WIDTH / 2 + (int)(cosf(angle) * s_colour_saturation * 144.0f);
    const int cy = 270 + (int)(sinf(angle) * s_colour_saturation * 144.0f);
    const int old_y = s_colour_marker_drawn_y;
    const bool moved = cx != s_colour_marker_drawn_x || cy != old_y;
    s_fluid_controls_transfer_pixels = 0;
    if (full) {
        fluid_controls_render_band(0, LCD_HEIGHT, true);
    } else if (moved) {
        fluid_colour_restore_marker(s_colour_marker_drawn_x, old_y);
    }
    if (full || moved) fluid_colour_draw_marker(cx, cy);
    const int64_t drawn = esp_timer_get_time();
    bool ok = true;
    if (full) {
        ok = fluid_controls_flush_band(0, LCD_HEIGHT);
    } else if (moved) {
        if (abs(cy - old_y) <= 13) {
            ok = fluid_controls_flush_band((cy < old_y ? cy : old_y) - 6,
                                           (cy > old_y ? cy : old_y) + 7);
        } else {
            ok = fluid_controls_flush_band(old_y - 6, old_y + 7);
            if (ok) ok = fluid_controls_flush_band(cy - 6, cy + 7);
        }
    }
    if (!ok) {
        s_fluid_controls_canvas_screen = UI_SCREEN_HOME;
        return;
    }
    s_colour_marker_drawn_x = cx;
    s_colour_marker_drawn_y = cy;
    s_fluid_controls_canvas_screen = UI_SCREEN_COLOUR_PICKER;
    s_colour_picker_dirty = false;
    fluid_controls_record_frame(started, drawn);
}

static void render_ouo_frame(void)
{
    if (s_lcd_panel == NULL || s_lcd_stripe == NULL || s_lcd_canvas == NULL ||
        s_lcd_done == NULL) {
        return;
    }

    const int64_t render_start_us = esp_timer_get_time();
    const bool idle_fast = s_ouo_idle_visible && s_ouo_idle_tiles != NULL &&
                           s_ouo_auto_expressions && !s_ouo_touch_active &&
                           s_ouo_expression == OUO_EXPRESSION_IDLE;
    /* This panel is reliable with full-width QSPI transfers.  The previous
     * implementation sent every one of its 221,350 pixels even though OuO
     * changes only its central face.  Keep the safe full-width window but
     * restrict dynamic updates to face rows; a first entry remains a full
     * black repaint so no prior Home pixels can survive. */
    dirty_rect_t dirty = {
        .x1 = 0,
        .y1 = s_ouo_canvas_valid ? 112 : 0,
        .x2 = LCD_WIDTH,
        .y2 = s_ouo_canvas_valid ? 370 : LCD_HEIGHT,
    };
    if (!s_ouo_canvas_valid) {
        memset(s_lcd_canvas, 0, LCD_FRAME_BYTES);
    } else {
        clear_canvas_rect(&dirty);
    }
    if (idle_fast) {
        ouo_blit_idle_tiles(&dirty);
    } else {
        for (int y = dirty.y1; y < dirty.y2; ++y) {
            uint16_t *line = s_lcd_canvas + (size_t)y * LCD_WIDTH;
            for (int x = 0; x < LCD_WIDTH; ++x) {
                line[x] = ouo_pixel(x, y);
            }
        }
    }
    const int64_t flush_start_us = esp_timer_get_time();
    if (flush_canvas_rect(&dirty)) {
        const int64_t render_end_us = esp_timer_get_time();
        const int64_t elapsed_us = render_end_us - render_start_us;
        s_ouo_render_total_us += elapsed_us;
        s_ouo_paint_window_us += flush_start_us - render_start_us;
        s_ouo_flush_window_us += render_end_us - flush_start_us;
        s_ouo_work_window_us += elapsed_us + s_ouo_update_last_us;
        s_ouo_decode_window_us += s_ouo_decode_last_us;
        ++s_ouo_render_frames;
        if ((s_ouo_render_frames % 20U) == 0U) {
            ESP_LOGI(TAG, "[0u0-PERF] frames=%lu avg render=%lldus draw=%lldus flush=%lldus work=%lldus decode=%lldus idle=%d region=%d px (%d%%)",
                     (unsigned long)s_ouo_render_frames,
                     (long long)(s_ouo_render_total_us / s_ouo_render_frames),
                     (long long)(s_ouo_paint_window_us / 20),
                     (long long)(s_ouo_flush_window_us / 20),
                     (long long)(s_ouo_work_window_us / 20),
                     (long long)(s_ouo_decode_window_us / 20), idle_fast,
                     LCD_WIDTH * (dirty.y2 - dirty.y1),
                     100 * (dirty.y2 - dirty.y1) / LCD_HEIGHT);
            s_ouo_paint_window_us = 0;
            s_ouo_flush_window_us = 0;
            s_ouo_work_window_us = 0;
            s_ouo_decode_window_us = 0;
        }
        s_ouo_canvas_valid = true;
        s_ouo_dirty = false;
    }
}

static void render_ouo_menu_frame(void)
{
    if (s_lcd_panel == NULL || s_lcd_stripe == NULL || s_lcd_done == NULL) {
        return;
    }
    for (int stripe_y = 0; stripe_y < LCD_HEIGHT; stripe_y += LCD_STRIPE_LINES) {
        const int stripe_end = clamp_int(stripe_y + LCD_STRIPE_LINES, 0, LCD_HEIGHT);
        for (int y = stripe_y; y < stripe_end; ++y) {
            uint16_t *line = s_lcd_stripe + (size_t)(y - stripe_y) * LCD_WIDTH;
            for (int x = 0; x < LCD_WIDTH; ++x) {
                line[x] = rgb565_for_sh8601(ouo_menu_pixel(x, y));
            }
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_lcd_panel, 0, stripe_y,
                                                   LCD_WIDTH, stripe_end, s_lcd_stripe);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "0u0 menu transfer failed: %s", esp_err_to_name(err));
            return;
        }
        if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
            ESP_LOGE(TAG, "0u0 menu DMA completion timed out");
            return;
        }
    }
    s_ouo_menu_dirty = false;
}

static void render_frame(void)
{
    if (s_lcd_panel == NULL || s_lcd_stripe == NULL || s_lcd_canvas == NULL ||
        s_lcd_done == NULL) {
        return;
    }

    s_fluid_transfer_pixels = 0;
    s_matrix_layout_ready = false;
    if (s_fluid_shape == FLUID_SHAPE_MATRIX) {
        prepare_matrix_layout();
    }
    if (!s_fluid_canvas_valid) {
        dirty_rect_t full = {.x1 = 0, .y1 = 0, .x2 = LCD_WIDTH, .y2 = LCD_HEIGHT};
        memset(s_lcd_canvas, 0, LCD_FRAME_BYTES);
        render_fluid_shape_into_canvas(NULL);
        if (flush_canvas_rect(&full)) {
            sync_drawn_particle_positions();
            s_fluid_canvas_valid = true;
            s_fluid_transfer_pixels = LCD_FRAME_PIXELS;
        }
        return;
    }

    /* This is the PACON equivalent of Opal's dirty canvas invalidation.  The
     * bounding region contains every prior and current droplet footprint, so
     * clearing it is safe and then repainting all intersecting droplets cannot
     * leave trails or erase an overlapping neighbour. */
    dirty_rect_t dirty = {.x1 = LCD_WIDTH, .y1 = LCD_HEIGHT, .x2 = 0, .y2 = 0};
    for (int particle = 0; particle < PARTICLE_COUNT; ++particle) {
        dirty_rect_include_particle(&dirty, s_drawn_x[particle], s_drawn_y[particle]);
        if (s_fluid_shape == FLUID_SHAPE_MATRIX &&
            s_matrix_x[particle] >= 0 && s_matrix_y[particle] >= 0) {
            dirty_rect_include_particle(&dirty,
                                        s_matrix_x[particle] + MATRIX_CELL_SIZE / 2,
                                        s_matrix_y[particle] + MATRIX_CELL_SIZE / 2);
        } else {
            dirty_rect_include_particle(&dirty, (int)(s_particles[particle].x >> 8),
                                        (int)(s_particles[particle].y >> 8));
        }
    }
    if (!dirty_rect_valid(&dirty)) {
        return;
    }

    /* The SH8601's QSPI stream is proven stable for full scan lines.  Narrow
     * X windows produced address-wrap ghosts on PACON, so retain the useful
     * dirty Y range but always send complete 475-pixel rows. */
    dirty.x1 = 0;
    dirty.x2 = LCD_WIDTH;

    clear_canvas_rect(&dirty);
    render_fluid_shape_into_canvas(&dirty);
    if (flush_canvas_rect(&dirty)) {
        sync_drawn_particle_positions();
        s_fluid_transfer_pixels = (size_t)(dirty.x2 - dirty.x1) *
                                  (size_t)(dirty.y2 - dirty.y1);
    }
}

static void usb_msc_poll_exit_touch(void)
{
    TickType_t now = xTaskGetTickCount();
    if (!s_touch_ready) {
        if (s_usb_msc_touch_retry_after == 0 ||
            (int32_t)(now - s_usb_msc_touch_retry_after) >= 0) {
            ESP_LOGW(TAG, "USB disk: touch unavailable; retrying FT3168 initialization");
            init_touch();
            s_usb_msc_touch_retry_after = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
        }
        if (!s_touch_ready) return;
        now = xTaskGetTickCount();
    }

    uint8_t count = 0;
    if (i2c_read(ADDR_TOUCH, 0x02, &count, 1) != ESP_OK) {
        s_touch_ready = false;
        s_usb_msc_touch_retry_after = now + pdMS_TO_TICKS(250);
        return;
    }
    if ((count & 0x0F) == 0) {
        s_usb_msc_exit_touch_down = false;
        return;
    }

    uint8_t point[4] = {0};
    if (i2c_read(ADDR_TOUCH, 0x03, point, sizeof(point)) != ESP_OK) return;
    const int x = ((point[0] & 0x0F) << 8) | point[1];
    const int y = ((point[2] & 0x0F) << 8) | point[3];
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) return;

    if (!s_usb_msc_exit_touch_down && s_usb_msc_exit_armed && y >= 330) {
        ESP_LOGW(TAG, "USB disk: raw OFF touch at (%d,%d); returning to normal mode", x, y);
        s_usb_msc_exit_requested = true;
    }
    s_usb_msc_exit_touch_down = true;
}

static esp_err_t usb_msc_restore_serial_jtag_phy(void)
{
    /* ESP32-S3 shares its internal FS/LS PHY between USB-OTG and the
     * always-on USB Serial/JTAG controller. usb_del_phy() releases OTG but
     * deliberately does not restore this mux, and esp_restart() does not
     * reset the RTC USB mux either. Claim the PHY for Serial/JTAG explicitly
     * before the CPU-only restart so Windows sees a real serial device again. */
    usb_phy_handle_t serial_jtag_phy = NULL;
    const usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_SERIAL_JTAG,
        .target = USB_PHY_TARGET_INT,
    };
    return usb_new_phy(&phy_config, &serial_jtag_phy);
}

static void usb_msc_disconnect_for_restart(void)
{
    if (s_usb_msc_started) {
        ESP_LOGI(TAG, "USB disk: detaching TinyUSB MSC before normal-mode restart");
        const esp_err_t err = tinyusb_driver_uninstall();
        if (err == ESP_OK) {
            s_usb_msc_started = false;
        } else {
            ESP_LOGE(TAG, "USB disk: TinyUSB detach failed: %s", esp_err_to_name(err));
        }
    }

    const esp_err_t serial_err = usb_msc_restore_serial_jtag_phy();
    if (serial_err != ESP_OK) {
        ESP_LOGE(TAG, "USB disk: Serial/JTAG PHY restore failed: %s",
                 esp_err_to_name(serial_err));
    } else {
        ESP_LOGI(TAG, "USB disk: internal PHY returned to USB Serial/JTAG");
    }

    /* Hold the MSC disconnect long enough for Windows to retire the disk
     * identity before the CPU-only restart continues in normal mode. */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static void run_usb_msc_screen_loop(void)
{
    while (true) {
        /* USB mode owns a raw reader so a stale page-transition contact or a
         * failed one-shot FT3168 init cannot permanently disable OFF. */
        usb_msc_poll_exit_touch();
        if (s_usb_msc_started && !s_usb_msc_exit_armed &&
            (int32_t)(xTaskGetTickCount() - s_usb_msc_exit_arm_after) >= 0) {
            s_usb_msc_exit_armed = true;
        }
        if (s_usb_msc_reboot_requested || s_usb_msc_exit_requested) {
            usb_msc_disconnect_for_restart();
        }
        /* Do not render or run unrelated services after TinyUSB takes the
         * native USB PHY.  This loop is deliberately small so MSC/SDMMC gets
         * a deterministic transfer environment. */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "PACON pendant starting: Fluid + 0u0 (reset_reason=%d)",
             (int)esp_reset_reason());
    ESP_LOGI(TAG, "Fluid uses tilt and touch; 0u0 reacts to eyes, forehead and cheeks.");
    ESP_LOGI(TAG, "Home media: NAND carousel with Miku-teal fallback");

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_NVS_INVALID_STATE) {
        ESP_LOGW(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
    }
    const bool usb_msc_boot = usb_msc_boot_requested();

    /* Load UI preferences before the panel is powered.  BLE is deliberately
     * started after the LCD DMA buffers have been allocated: the NimBLE
     * controller consumes internal RAM, while this board needs two large
     * internal-DMA stripes for the SH8601. */
    settings_load_preferences();
    clock_load_preferences();
    feature_load_preferences();

    /* Bring up the shared I2C bus and PMIC first.  The panel power gate stays
     * low until init_lcd(), so its OLED matrix cannot light during rail setup. */
    err = lcd_hold_power_off();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD power hold-off failed: %s", esp_err_to_name(err));
        return;
    }
    err = init_i2c();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(err));
        return;
    }
    probe_axp2101();
    s_clock_cached_time = clock_read_rtc();
    vTaskDelay(pdMS_TO_TICKS(20));

    err = init_lcd();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SH8601 initialization failed: %s", esp_err_to_name(err));
        return;
    }
    if (!usb_msc_boot) {
    /* Start the phone control channel after the display's internal DMA
     * allocations so BLE cannot starve the SH8601 panel setup. */
    s_ble_ui_requests = xQueueCreate(1, sizeof(ui_screen_t));
    if (s_ble_ui_requests == NULL) ESP_LOGE(TAG, "BLE UI request queue allocation failed");
    ble_pacon_set_command_handler(fluid_ble_command);
    ble_pacon_set_binary_handler(fluid_ble_media_binary);
    ble_pacon_init();
    init_particles();
    init_touch();
    init_imu();
    init_sd_nand_read_only_probe();
    if (!init_home_external_media()) {
        ESP_LOGW(TAG, "Media: NAND assets unavailable; continuing with Miku-teal fallback");
    }

    /* Restore network intent only after LCD DMA, BLE and media allocations.
     * Never persist live link state or open USB storage automatically. */
    skyorb_load_config();
    if (pacon_load_switch("wifi_on", false)) {
        skyorb_start_network_task();
        if (pacon_load_switch("wifi_join", false) && s_skyorb_config.wifi_valid) {
            (void)wifi_request_profile_connection(s_skyorb_config.wifi_selected);
        }
    }
    } else {
        /* The USB screen remains interactive so the user can safely eject on
         * the PC and tap OFF to reboot back into the full application. */
        init_touch();
        s_usb_msc_exit_armed = false;
        s_usb_msc_exit_arm_after = xTaskGetTickCount() + pdMS_TO_TICKS(1500);
        block_touch_until_release();
        /* Finish every local display/touch initialization and paint once
         * before changing the USB PHY.  There are no display transfers after
         * TinyUSB starts, so Windows can probe the disk without contention. */
        s_ui_screen = UI_SCREEN_USB_DISK;
        s_usb_msc_ui_on = true;
        s_usb_disk_dirty = true;
        render_usb_disk_frame();
        s_usb_msc_result = start_usb_msc_mode();
        if (s_usb_msc_result != ESP_OK) {
            s_usb_msc_ui_on = false;
            s_usb_disk_dirty = true;
            render_usb_disk_frame();
        }
        run_usb_msc_screen_loop();
    }

    TickType_t last_frame = xTaskGetTickCount();
    TickType_t next_pmic_status = last_frame + pdMS_TO_TICKS(PMIC_STATUS_PERIOD_MS);
    TickType_t next_home_refresh = last_frame;
    s_last_user_activity = last_frame;
    s_next_home_status_shift = last_frame + pdMS_TO_TICKS(HOME_STATUS_SHIFT_MS);
    s_next_home_pixel_shift = last_frame + pdMS_TO_TICKS(HOME_PIXEL_SHIFT_MS);
    int64_t next_perf_report_us = esp_timer_get_time() + FLUID_PERF_REPORT_MS * 1000LL;
    int64_t physics_total_us = 0;
    int64_t render_total_us = 0;
    uint64_t transfer_pixels_total = 0;
    uint32_t fluid_frames = 0;
    while (true) {
        TickType_t now = xTaskGetTickCount();
        apply_ble_brightness_if_pending();
        apply_ble_ui_request();
        if (s_ui_screen != UI_SCREEN_FLUID_SETTINGS && s_ui_screen != UI_SCREEN_COLOUR_PICKER) {
            s_fluid_controls_canvas_screen = UI_SCREEN_HOME;
            s_colour_dragging = false;
            s_fluid_controls_input_us = 0;
        }
        clock_service(now);
        apply_home_media_rescan_if_pending();
        home_update_burnin_offsets(now);
        display_update_idle(now);
        if (s_display_sleeping) {
            /* FT3168 remains powered; its next complete touch wakes OLED but
             * is suppressed so a sleeping screen cannot launch an app. */
            poll_touch();
        } else if (s_ui_screen == UI_SCREEN_HOME) {
            poll_touch();
            if (s_ui_screen == UI_SCREEN_HOME && s_home_slide_waiting) {
                (void)home_begin_pending_media_transition(now);
            }
            if (s_ui_screen == UI_SCREEN_HOME && s_home_slide_active) {
                render_home_slide_frame();
            } else if (s_ui_screen == UI_SCREEN_HOME &&
                (s_home_dirty || home_advance_animation(now) ||
                 (int32_t)(now - next_home_refresh) >= 0)) {
                render_home_frame();
                next_home_refresh = now + pdMS_TO_TICKS(HOME_REFRESH_MS);
            }
        } else if (s_ui_screen == UI_SCREEN_APPS) {
            poll_touch();
            if (s_ui_screen == UI_SCREEN_APPS &&
                (s_apps_dirty || s_apps_dismiss_active)) {
                render_apps_frame();
            }
        } else if (s_ui_screen == UI_SCREEN_USB_DISK) {
            /* Sample release once before the slow first full-frame paint.  A
             * fast second tap must not be swallowed as the launcher contact. */
            poll_touch();
            /* Paint the transition screen before USB re-enumerates, because
             * the serial monitor disappears as soon as the MSC device owns
             * the native USB data pair. */
            if (s_usb_disk_dirty) {
                render_usb_disk_frame();
            }
            poll_touch();
            if (s_usb_msc_started && !s_usb_msc_exit_armed &&
                !s_touch_blocked_until_release &&
                (int32_t)(xTaskGetTickCount() - s_usb_msc_exit_arm_after) >= 0) {
                s_usb_msc_exit_armed = true;
            }
            if (s_usb_msc_reboot_requested) {
                /* Keep USB Serial/JTAG alive until the transition screen has
                 * been painted, then restart into a minimal MSC-only boot. */
                vTaskDelay(pdMS_TO_TICKS(250));
                esp_restart();
            }
            if (s_usb_msc_exit_requested) {
                usb_msc_disconnect_for_restart();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        } else if (s_ui_screen == UI_SCREEN_FLUID_SETTINGS) {
            poll_touch();
            if (s_ui_screen == UI_SCREEN_FLUID_SETTINGS && s_settings_dirty) {
                render_fluid_settings_frame();
            }
        } else if (s_ui_screen == UI_SCREEN_COLOUR_PICKER) {
            poll_touch();
            if (s_ui_screen == UI_SCREEN_COLOUR_PICKER && s_colour_picker_dirty) {
                render_fluid_colour_picker_frame();
            }
        } else if (s_ui_screen == UI_SCREEN_OUO) {
            const int64_t update_start_us = esp_timer_get_time();
            poll_touch();
            step_ouo();
            s_ouo_update_last_us = esp_timer_get_time() - update_start_us;
            if (s_ui_screen == UI_SCREEN_OUO && s_ouo_dirty) {
                render_ouo_frame();
            }
        } else if (s_ui_screen == UI_SCREEN_OUO_MENU) {
            poll_touch();
            if (s_ui_screen == UI_SCREEN_OUO_MENU && s_ouo_menu_dirty) {
                render_ouo_menu_frame();
            }
        } else if (s_ui_screen == UI_SCREEN_SKYORB) {
            poll_touch();
            if (s_ui_screen == UI_SCREEN_SKYORB &&
                (s_skyorb_dirty || s_skyorb_last_frame == 0 ||
                 (int32_t)(now - s_skyorb_last_frame) >=
                    (int32_t)pdMS_TO_TICKS(SKYORB_FRAME_PERIOD_MS))) {
                s_skyorb_sweep_angle = (uint16_t)((s_skyorb_sweep_angle + 9U) % 360U);
                skyorb_render_frame();
                s_skyorb_last_frame = xTaskGetTickCount();
            }
        } else if (s_ui_screen == UI_SCREEN_WATCH) {
            poll_touch();
            if (s_ui_screen == UI_SCREEN_WATCH &&
                (s_watch_dirty || s_watch_last_frame == 0 ||
                 (int32_t)(now - s_watch_last_frame) >=
                    (int32_t)pdMS_TO_TICKS(1000))) {
                /* Anchor the cadence at frame start.  Anchoring after the
                 * expensive layered render adds its duration to every second. */
                s_watch_last_frame = now;
                watch_render_frame();
            }
        } else if (s_ui_screen == UI_SCREEN_CAMERA) {
            poll_touch();
            now = xTaskGetTickCount();
            const bool feedback_active = s_camera_feedback_until != 0 &&
                (int32_t)(s_camera_feedback_until - now) > 0;
            if (s_ui_screen == UI_SCREEN_CAMERA &&
                (s_camera_dirty || feedback_active || s_camera_last_frame == 0 ||
                 (int32_t)(now - s_camera_last_frame) >= pdMS_TO_TICKS(250))) {
                camera_render_frame();
            }
            vTaskDelay(pdMS_TO_TICKS(feedback_active ? 35 : 80));
        } else if (s_ui_screen == UI_SCREEN_MIC_TEST) {
            poll_touch();
            now = xTaskGetTickCount();
            if (s_ui_screen == UI_SCREEN_MIC_TEST &&
                (s_mic_test_dirty || s_mic_test_last_frame == 0 ||
                 (int32_t)(now - s_mic_test_last_frame) >= pdMS_TO_TICKS(80))) {
                mic_test_render_frame();
            }
        } else if (s_ui_screen == UI_SCREEN_SETTINGS) {
            poll_touch();
            if (s_ui_screen == UI_SCREEN_SETTINGS && s_device_settings_dirty) {
                render_device_settings_frame();
            }
        } else if (s_ui_screen == UI_SCREEN_WIFI_SETTINGS) {
            poll_touch();
            now = xTaskGetTickCount();
            if (s_wifi_should_connect && !s_skyorb_wifi_connected &&
                (int32_t)(now - s_wifi_settings_last_frame) >= pdMS_TO_TICKS(350)) {
                s_wifi_settings_dirty = true;
            }
            if (s_ui_screen == UI_SCREEN_WIFI_SETTINGS && s_wifi_settings_dirty) {
                render_wifi_settings_frame();
            }
        } else {
            int64_t physics_start_us = esp_timer_get_time();
            /* Keep the fluid lively even if the display transfer uses a full frame. */
            step_fluid();
            step_fluid();
            physics_total_us += esp_timer_get_time() - physics_start_us;

            now = xTaskGetTickCount();
            int64_t render_start_us = esp_timer_get_time();
            render_frame();
            render_total_us += esp_timer_get_time() - render_start_us;
            transfer_pixels_total += s_fluid_transfer_pixels;
            ++fluid_frames;

            int64_t now_us = esp_timer_get_time();
            if (now_us >= next_perf_report_us && fluid_frames != 0) {
                ESP_LOGI(TAG, "[FLUID-PERF] frames=%lu avg physics=%lldus render=%lldus dirty=%lu px (%.0f%%)",
                         (unsigned long)fluid_frames, physics_total_us / (int64_t)fluid_frames,
                         render_total_us / (int64_t)fluid_frames,
                         (unsigned long)(transfer_pixels_total / fluid_frames),
                         100.0 * (double)(transfer_pixels_total / fluid_frames) /
                         (double)LCD_FRAME_PIXELS);
                physics_total_us = 0;
                render_total_us = 0;
                transfer_pixels_total = 0;
                fluid_frames = 0;
                next_perf_report_us = now_us + FLUID_PERF_REPORT_MS * 1000LL;
            }
        }

        fluid_service_preferences();
        fluid_controls_report_perf();
        now = xTaskGetTickCount();
        if (s_axp2101_ready && (int32_t)(now - next_pmic_status) >= 0) {
            log_axp2101_charge_status();
            next_pmic_status += pdMS_TO_TICKS(PMIC_STATUS_PERIOD_MS);
        }
        /* OuO's animated eyes need the previewer's ~60 FPS cadence.  Keep the
         * heavier Fluid/home loop at its original 30 ms period. */
        const uint32_t frame_period_ms = (s_ui_screen == UI_SCREEN_OUO ||
            s_ui_screen == UI_SCREEN_FLUID_SETTINGS || s_ui_screen == UI_SCREEN_COLOUR_PICKER) ?
                                         OUO_FRAME_PERIOD_MS : FRAME_PERIOD_MS;
        vTaskDelayUntil(&last_frame, pdMS_TO_TICKS(frame_period_ms));
    }
}
