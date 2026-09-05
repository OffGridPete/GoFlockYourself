/**
 * GoFlockYourself — queue, de-dupe, history
 */
#include "detection.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

namespace gfy {

static Settings s_cfg;
static Stats s_stats;
static QueueHandle_t s_q = nullptr;

static HistoryEntry s_hist[HISTORY_MAX];
static size_t s_hist_count = 0;
static size_t s_hist_head = 0;  // next write index

// Last alerted event for status UI
static DetectEvent s_last;
static uint32_t s_last_ms = 0;
static bool s_have_last = false;

// Per-MAC cooldown table (static, small)
struct CoolSlot {
  uint8_t mac[6];
  uint32_t last_ms;
  bool used;
};
static CoolSlot s_cool[24];

static bool mac_eq(const uint8_t *a, const uint8_t *b) {
  return memcmp(a, b, 6) == 0;
}

static bool should_alert(const uint8_t *mac, uint32_t now) {
  // Find existing
  for (size_t i = 0; i < 24; i++) {
    if (s_cool[i].used && mac_eq(s_cool[i].mac, mac)) {
      if (now - s_cool[i].last_ms < s_cfg.alert_cooldown_ms) {
        return false;
      }
      s_cool[i].last_ms = now;
      return true;
    }
  }
  // Free slot
  for (size_t i = 0; i < 24; i++) {
    if (!s_cool[i].used) {
      memcpy(s_cool[i].mac, mac, 6);
      s_cool[i].last_ms = now;
      s_cool[i].used = true;
      return true;
    }
  }
  // Evict oldest
  size_t oldest = 0;
  for (size_t i = 1; i < 24; i++) {
    if (s_cool[i].last_ms < s_cool[oldest].last_ms) oldest = i;
  }
  memcpy(s_cool[oldest].mac, mac, 6);
  s_cool[oldest].last_ms = now;
  return true;
}

static bool is_unique_mac(const uint8_t *mac) {
  for (size_t i = 0; i < s_hist_count; i++) {
    size_t idx = (s_hist_head + HISTORY_MAX - 1 - i) % HISTORY_MAX;
    if (mac_eq(s_hist[idx].mac, mac)) return false;
  }
  return true;
}

static void history_push(const DetectEvent &ev, uint32_t now) {
  HistoryEntry &e = s_hist[s_hist_head];
  memcpy(e.mac, ev.mac, 6);
  e.rssi = ev.rssi;
  e.channel = ev.channel;
  e.method = ev.method;
  e.protocol = ev.protocol;
  e.confidence = ev.confidence;
  e.millis_at = now;

  s_hist_head = (s_hist_head + 1) % HISTORY_MAX;
  if (s_hist_count < HISTORY_MAX) s_hist_count++;
}

void detection_init() {
  memset(&s_cfg, 0, sizeof(s_cfg));
  memset(&s_stats, 0, sizeof(s_stats));
  memset(s_cool, 0, sizeof(s_cool));
  memset(s_hist, 0, sizeof(s_hist));
  s_hist_count = 0;
  s_hist_head = 0;
  s_have_last = false;

  s_cfg.scanning = true;
  s_cfg.wifi_oui_tx = DEF_WIFI_OUI_TX;
  s_cfg.wifi_oui_rx = DEF_WIFI_OUI_RX;
  s_cfg.wildcard_probe = DEF_WILDCARD_PROBE;
  s_cfg.broad_oui = DEF_BROAD_OUI;
  s_cfg.ssid_keywords = DEF_SSID_KEYWORDS;
  s_cfg.ble_scan = DEF_BLE_SCAN;
  s_cfg.audio_alert = DEF_AUDIO_ALERT;
  s_cfg.led_alert = DEF_LED_ALERT;
  s_cfg.invert_display = DEF_INVERT_DISPLAY;
  // logger_init() sets this true only when a card is actually present
  s_cfg.sd_logging = false;
  s_cfg.chan_plan = DEFAULT_CHAN_PLAN;
  s_cfg.dwell_ms = CHANNEL_DWELL_MS;
  s_cfg.rssi_floor = RSSI_FLOOR;
  s_cfg.alert_cooldown_ms = ALERT_COOLDOWN_MS;

  s_q = xQueueCreate(DETECT_QUEUE_LEN, sizeof(DetectEvent));
}

Settings &settings() { return s_cfg; }
Stats &stats() { return s_stats; }

bool detection_enqueue(const DetectEvent &ev) {
  if (!s_q) return false;
  // Non-blocking — drop if full (sniffer must stay light)
  return xQueueSend(s_q, &ev, 0) == pdTRUE;
}

uint8_t detection_drain(HitCallback on_hit) {
  if (!s_q) return 0;
  uint8_t n = 0;
  DetectEvent ev;
  const uint32_t now = millis();

  while (xQueueReceive(s_q, &ev, 0) == pdTRUE) {
    n++;
    s_stats.hits_total++;

    const bool uniq = is_unique_mac(ev.mac);
    if (uniq) s_stats.hits_unique++;

    history_push(ev, now);

    s_last = ev;
    s_last_ms = now;
    s_have_last = true;

    const bool alert = should_alert(ev.mac, now);
    if (on_hit) on_hit(ev, alert);
  }
  return n;
}

const HistoryEntry *history_get(size_t index) {
  if (index >= s_hist_count) return nullptr;
  size_t idx = (s_hist_head + HISTORY_MAX - 1 - index) % HISTORY_MAX;
  return &s_hist[idx];
}

size_t history_count() { return s_hist_count; }

void history_clear() {
  s_hist_count = 0;
  s_hist_head = 0;
  memset(s_hist, 0, sizeof(s_hist));
  memset(s_cool, 0, sizeof(s_cool));
}

bool last_hit(DetectEvent &out, uint32_t &millis_at) {
  if (!s_have_last) return false;
  out = s_last;
  millis_at = s_last_ms;
  return true;
}

}  // namespace gfy
