/**
 * GoFlockYourself — Configuration
 * Target: ESP32-2432S028R (Cheap Yellow Display)
 *
 * Passive Flock Safety ALPR detector via WiFi promiscuous mode.
 * Memory-conscious: static allocation, minimal heap use, no PSRAM required.
 */
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Project identity
// ---------------------------------------------------------------------------
#define GFY_NAME            "GoFlockYourself"
#define GFY_VERSION         "1.1.0"
#define GFY_HW_NAME         "ESP32-2432S028R (CYD)"

// ---------------------------------------------------------------------------
// Hardware pins — ESP32-2432S028R
// ---------------------------------------------------------------------------
// TFT (HSPI) is configured via TFT_eSPI build_flags in platformio.ini
// Touch uses a separate VSPI bus (different pins from display)
#define TOUCH_IRQ           36
#define TOUCH_MOSI          32
#define TOUCH_MISO          39
#define TOUCH_CLK           25
#define TOUCH_CS            33

// Onboard RGB LED (active LOW on most CYD boards)
#define LED_R               4
#define LED_G               16
#define LED_B               17
#define LED_ACTIVE_LOW      1

// Passive piezo buzzer — CN1 / P3 header GPIO22 (common free pin)
// Wire: buzzer+ → GPIO22, buzzer- → GND (or use transistor for louder)
#define BUZZER_PIN          22
#define BUZZER_ACTIVE_HIGH  1
#define BEEP_FREQ_HZ        2400
#define BEEP_MS             90

// microSD (shared VSPI pins with SD slot on CYD)
#define SD_CS               5
#define SD_MOSI             23
#define SD_MISO             19
#define SD_SCK              18
#define ENABLE_SD_LOGGING   1

// Display geometry
#define TFT_W               240
#define TFT_H               320

// ---------------------------------------------------------------------------
// Detection engine
// ---------------------------------------------------------------------------
#ifndef GFY_ENABLE_BLE
#define GFY_ENABLE_BLE      1
#endif

// Channel plans
#define CHAN_PLAN_PRIMARY   0   // 1, 6, 11 (fastest cover of busy channels)
#define CHAN_PLAN_FULL      1   // 1..13
#define CHAN_PLAN_ASC       2   // 1..11 ascending (wifi-recon style)

#define DEFAULT_CHAN_PLAN   CHAN_PLAN_PRIMARY
#define CHANNEL_DWELL_MS    280   // ms per channel; ~2x observed Flock probe interval
#define RSSI_FLOOR          -95   // ignore weaker frames

// Feature toggles (runtime-mutable via UI; these are boot defaults)
#define DEF_WIFI_OUI_TX     1
#define DEF_WIFI_OUI_RX     1
#define DEF_WILDCARD_PROBE  1
#define DEF_BROAD_OUI       0   // any frame OUI (noisier)
#define DEF_SSID_KEYWORDS   1
#define DEF_BLE_SCAN        0   // off by default; radio stays on WiFi
#define DEF_AUDIO_ALERT     1
#define DEF_LED_ALERT       1
// CYD panel lots disagree on invert. First boot asks "tap the dark side";
// the choice is stored in NVS and can be changed from the main menu.
#define DEF_INVERT_DISPLAY  0

// Alert de-duplication
#define ALERT_COOLDOWN_MS   4000UL   // same MAC won't re-alert within this window
#define ALERT_DISPLAY_MS    5500UL   // full-screen alert duration
#define DETECT_QUEUE_LEN    16

// History / log
#define HISTORY_MAX         40       // in-RAM ring of detections
#define SD_LOG_PATH         "/gfy_log.csv"

// BLE time-share (only when BLE enabled at runtime)
// Classic ESP32: full WiFi stop/start around each BLE window — keep BLE short.
#define WIFI_SLICE_MS       12000UL
#define BLE_SCAN_MS         2000UL
#define BLE_MFG_XUNTONG     0x09C8

// UI timing
#define UI_TICK_MS          50
#define STATUS_REFRESH_MS   250
#define HEARTBEAT_MS        15000UL
#define HEAP_WARN_BYTES     12000

// Touch calibration (typical CYD resistive — adjust if needed)
#define TOUCH_X_MIN         200
#define TOUCH_X_MAX         3700
#define TOUCH_Y_MIN         240
#define TOUCH_Y_MAX         3800
#define TOUCH_Z_THRESHOLD   400
