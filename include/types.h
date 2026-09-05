/**
 * GoFlockYourself — shared types
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

// Detection method / confidence tier
enum DetectMethod : uint8_t {
  METH_WILDCARD_PROBE = 0,  // OUI + probe req + empty SSID  (highest)
  METH_PROBE_OUI      = 1,  // OUI + any probe request
  METH_OUI_TX         = 2,  // transmitter OUI on mgmt/data
  METH_OUI_RX         = 3,  // receiver (addr1) OUI
  METH_SSID_KEYWORD   = 4,  // SSID name fragment
  METH_BLE_OUI        = 5,
  METH_BLE_MFG        = 6,
  METH_BLE_NAME       = 7,
  METH_BLE_UUID       = 8,
  METH_LITEON_IE      = 9,  // vendor IE 50:6F:9A on probe
};

enum Protocol : uint8_t {
  PROTO_WIFI = 0,
  PROTO_BLE  = 1,
};

// Compact event posted from radio/BLE paths → main loop
struct DetectEvent {
  uint8_t  mac[6];
  int8_t   rssi;
  uint8_t  channel;
  uint8_t  method;      // DetectMethod
  uint8_t  protocol;    // Protocol
  uint8_t  confidence;  // 0–100
  uint8_t  oui_conf;    // 0 high / 1 med / 2 low / 255 n/a
  char     label[20];   // ssid / ble name snippet
};

// In-RAM history entry
struct HistoryEntry {
  uint8_t  mac[6];
  int8_t   rssi;
  uint8_t  channel;
  uint8_t  method;
  uint8_t  protocol;
  uint8_t  confidence;
  uint32_t millis_at;   // millis() when recorded
};

// Runtime settings (mutable from UI)
struct Settings {
  bool scanning;
  bool wifi_oui_tx;
  bool wifi_oui_rx;
  bool wildcard_probe;
  bool broad_oui;
  bool ssid_keywords;
  bool ble_scan;
  bool audio_alert;
  bool led_alert;
  bool sd_logging;
  bool invert_display;    // some CYD panel lots invert black/white
  uint8_t chan_plan;      // CHAN_PLAN_*
  uint16_t dwell_ms;
  int8_t  rssi_floor;
  uint16_t alert_cooldown_ms;
};

// Global stats (updated from radio + main)
struct Stats {
  volatile uint32_t rx_frames;
  volatile uint32_t rx_mgmt;
  volatile uint32_t rx_probe;
  volatile uint32_t rx_wildcard;
  volatile uint32_t rx_beacon;
  volatile uint32_t oui_sightings;
  volatile uint32_t hits_total;
  volatile uint32_t hits_unique;
  volatile uint32_t ble_adv;
  volatile uint8_t  current_channel;
  volatile bool     radio_ok;
};

inline const char *method_name(uint8_t m) {
  switch (m) {
    case METH_WILDCARD_PROBE: return "WILD PROBE";
    case METH_PROBE_OUI:      return "PROBE OUI";
    case METH_OUI_TX:         return "OUI TX";
    case METH_OUI_RX:         return "OUI RX";
    case METH_SSID_KEYWORD:   return "SSID KEY";
    case METH_BLE_OUI:        return "BLE OUI";
    case METH_BLE_MFG:        return "BLE MFG";
    case METH_BLE_NAME:       return "BLE NAME";
    case METH_BLE_UUID:       return "BLE UUID";
    case METH_LITEON_IE:      return "LITEON IE";
    default:                  return "UNKNOWN";
  }
}

inline uint8_t method_score(uint8_t m) {
  switch (m) {
    case METH_WILDCARD_PROBE: return 100;
    case METH_LITEON_IE:      return 95;
    case METH_PROBE_OUI:      return 85;
    case METH_SSID_KEYWORD:   return 80;
    case METH_BLE_UUID:       return 75;
    case METH_BLE_MFG:        return 70;
    case METH_BLE_NAME:       return 65;
    case METH_OUI_TX:         return 60;
    case METH_BLE_OUI:        return 55;
    case METH_OUI_RX:         return 50;
    default:                  return 40;
  }
}

inline void mac_to_str(const uint8_t *mac, char *out, size_t n) {
  if (!out || n < 18) return;
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

inline void oui_to_str(const uint8_t *mac, char *out, size_t n) {
  if (!out || n < 9) return;
  snprintf(out, n, "%02X:%02X:%02X", mac[0], mac[1], mac[2]);
}
