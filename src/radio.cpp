/**
 * GoFlockYourself — WiFi promiscuous sniffer + channel hop + optional BLE
 *
 * Sniffer callback does minimal work: parse frame, match OUI, enqueue.
 * No Serial, no malloc, no TFT inside the callback.
 */
#include "radio.h"
#include "config.h"
#include "detection.h"
#include "oui_list.h"
#include "types.h"
#include "logger.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <string.h>
#include <ctype.h>

#if GFY_ENABLE_BLE
#include <NimBLEDevice.h>
#include <esp_bt.h>
#endif

namespace gfy {

// ---------------------------------------------------------------------------
// Channel tables
// ---------------------------------------------------------------------------
static const uint8_t CH_PRIMARY[] = { 1, 6, 11 };
static const uint8_t CH_FULL[]    = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
static const uint8_t CH_ASC[]     = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

static const uint8_t *s_ch_tbl = CH_PRIMARY;
static uint8_t s_ch_count = sizeof(CH_PRIMARY);
static uint8_t s_ch_idx = 0;
static uint32_t s_last_hop = 0;
static bool s_scanning = false;
static bool s_wifi_started = false;

// BLE time-share
static uint32_t s_slice_start = 0;
static bool s_in_ble = false;

#define WIFI_MAC_HDR_MIN    24
#define WIFI_FC_TYPE_MGMT   0
#define WIFI_MGMT_PROBE_REQ 4
#define WIFI_MGMT_PROBE_RES 5
#define WIFI_MGMT_BEACON    8

// ---------------------------------------------------------------------------
// MAC helpers
// ---------------------------------------------------------------------------
static inline bool mac_is_multicast(const uint8_t *m) {
  return (m[0] & 0x01) != 0;
}

static inline bool mac_is_zero_or_ff(const uint8_t *m) {
  bool z = true, f = true;
  for (int i = 0; i < 6; i++) {
    if (m[i] != 0x00) z = false;
    if (m[i] != 0xFF) f = false;
  }
  return z || f;
}

// ---------------------------------------------------------------------------
// Probe IE helpers
// ---------------------------------------------------------------------------
static int is_wildcard_ssid(const uint8_t *body, int len) {
  if (!body || len < 2) return -1;
  while (len >= 2) {
    const uint8_t id = body[0];
    const uint8_t elen = body[1];
    if ((int)elen + 2 > len) break;
    if (id == 0) return (elen == 0) ? 1 : 0;
    body += elen + 2;
    len -= elen + 2;
  }
  return -1;
}

static int extract_ssid(const uint8_t *body, int len, char *out, size_t outn) {
  if (!body || len < 2 || !out || outn < 2) return -1;
  out[0] = 0;
  while (len >= 2) {
    const uint8_t id = body[0];
    const uint8_t elen = body[1];
    if ((int)elen + 2 > len) break;
    if (id == 0) {
      size_t n = elen;
      if (n >= outn) n = outn - 1;
      memcpy(out, body + 2, n);
      out[n] = 0;
      return (int)n;
    }
    body += elen + 2;
    len -= elen + 2;
  }
  return -1;
}

static bool has_liteon_ie(const uint8_t *body, int len) {
  if (!body || len < 6) return false;
  while (len >= 2) {
    const uint8_t id = body[0];
    const uint8_t elen = body[1];
    if ((int)elen + 2 > len) break;
    if (id == 221 && elen >= 4 &&
        body[2] == 0x50 && body[3] == 0x6F && body[4] == 0x9A) {
      return true;
    }
    body += elen + 2;
    len -= elen + 2;
  }
  return false;
}

static bool keyword_hit(const char *s) {
  if (!s || !s[0]) return false;
  char buf[33];
  size_t i = 0;
  for (; s[i] && i < 32; i++) {
    buf[i] = (char)tolower((unsigned char)s[i]);
  }
  buf[i] = 0;
  for (size_t k = 0; k < SSID_KEYWORD_COUNT; k++) {
    if (strstr(buf, SSID_KEYWORDS[k])) return true;
  }
  return false;
}

static void post_hit(const uint8_t *mac, int8_t rssi, uint8_t ch,
                     uint8_t method, uint8_t proto, int oui_conf,
                     const char *label) {
  DetectEvent ev;
  memset(&ev, 0, sizeof(ev));
  memcpy(ev.mac, mac, 6);
  ev.rssi = rssi;
  ev.channel = ch;
  ev.method = method;
  ev.protocol = proto;
  ev.confidence = method_score(method);
  if (oui_conf == 1 && ev.confidence > 10) ev.confidence -= 10;
  if (oui_conf == 2 && ev.confidence > 20) ev.confidence -= 20;
  ev.oui_conf = (oui_conf < 0) ? 255 : (uint8_t)oui_conf;
  if (label && label[0]) {
    strncpy(ev.label, label, sizeof(ev.label) - 1);
  }
  detection_enqueue(ev);
}

// ---------------------------------------------------------------------------
// Promiscuous callback — keep it lean (no Serial / no malloc)
// ---------------------------------------------------------------------------
static void wifi_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
  Stats &st = stats();
  st.rx_frames++;

  if (type == WIFI_PKT_MGMT) st.rx_mgmt++;
  else if (type != WIFI_PKT_DATA) return;
  if (!buf) return;

  const wifi_promiscuous_pkt_t *ppkt = (const wifi_promiscuous_pkt_t *)buf;
  const uint8_t *payload = ppkt->payload;
  int sig_len = (int)ppkt->rx_ctrl.sig_len;
  if (sig_len < WIFI_MAC_HDR_MIN) return;

  const int8_t rssi = ppkt->rx_ctrl.rssi;
  Settings &cfg = settings();
  if (rssi < cfg.rssi_floor) return;

  const uint8_t fc0 = payload[0];
  const uint8_t ftype = (fc0 >> 2) & 0x03;
  const uint8_t fsubtype = (fc0 >> 4) & 0x0F;
  const uint8_t ch = ppkt->rx_ctrl.channel;
  const uint8_t *addr1 = payload + 4;
  const uint8_t *addr2 = payload + 10;

  if (type == WIFI_PKT_MGMT && ftype == WIFI_FC_TYPE_MGMT &&
      fsubtype == WIFI_MGMT_BEACON) {
    st.rx_beacon++;
  }

  const int conf2 = oui_match(addr2);
  const int conf1 = oui_match(addr1);
  if (conf2 >= 0 || conf1 >= 0) st.oui_sightings++;

  // ---- Probe requests (primary Flock path) ----
  if (type == WIFI_PKT_MGMT && ftype == WIFI_FC_TYPE_MGMT &&
      fsubtype == WIFI_MGMT_PROBE_REQ) {
    st.rx_probe++;

    int body_len = sig_len - WIFI_MAC_HDR_MIN;
    if (body_len > 4) body_len -= 4;
    const uint8_t *body = payload + WIFI_MAC_HDR_MIN;

    char ssid[33] = { 0 };
    int ssid_len = (body_len > 0)
                       ? extract_ssid(body, body_len, ssid, sizeof(ssid))
                       : -1;
    int wild = (body_len > 0) ? is_wildcard_ssid(body, body_len) : -1;
    if (wild == 1) st.rx_wildcard++;

    if (cfg.ssid_keywords && ssid_len > 0 && keyword_hit(ssid)) {
      post_hit(addr2, rssi, ch, METH_SSID_KEYWORD, PROTO_WIFI, conf2, ssid);
      return;
    }

    if (conf2 >= 0 && !mac_is_multicast(addr2) && !mac_is_zero_or_ff(addr2)) {
      bool liteon = (body_len > 0) && has_liteon_ie(body, body_len);
      const char *lab = (wild == 1)    ? "<wildcard>"
                        : (ssid_len > 0) ? ssid
                                         : "<probe>";

      if (liteon) {
        post_hit(addr2, rssi, ch, METH_LITEON_IE, PROTO_WIFI, conf2, lab);
        return;
      }
      if (cfg.wildcard_probe && wild == 1) {
        post_hit(addr2, rssi, ch, METH_WILDCARD_PROBE, PROTO_WIFI, conf2, lab);
        return;
      }
      if (cfg.wifi_oui_tx) {
        post_hit(addr2, rssi, ch, METH_PROBE_OUI, PROTO_WIFI, conf2, lab);
        return;
      }
    }
    return;
  }

  // ---- SSID keywords on beacon / probe response ----
  if (cfg.ssid_keywords && type == WIFI_PKT_MGMT && ftype == WIFI_FC_TYPE_MGMT &&
      (fsubtype == WIFI_MGMT_BEACON || fsubtype == WIFI_MGMT_PROBE_RES)) {
    int off = WIFI_MAC_HDR_MIN + 12;
    int body_len = sig_len - off - 4;
    if (body_len < 2) body_len = sig_len - off;
    if (body_len > 2) {
      char ssid[33];
      if (extract_ssid(payload + off, body_len, ssid, sizeof(ssid)) > 0 &&
          keyword_hit(ssid)) {
        post_hit(addr2, rssi, ch, METH_SSID_KEYWORD, PROTO_WIFI, conf2, ssid);
        return;
      }
    }
  }

  // ---- addr1 (receiver) OUI ----
  if (cfg.wifi_oui_rx && type == WIFI_PKT_MGMT && ftype == WIFI_FC_TYPE_MGMT &&
      conf1 >= 0 && !mac_is_multicast(addr1) && !mac_is_zero_or_ff(addr1)) {
    post_hit(addr1, rssi, ch, METH_OUI_RX, PROTO_WIFI, conf1, nullptr);
    return;
  }

  // ---- Broad OUI on any mgmt/data transmitter ----
  if (cfg.broad_oui && conf2 >= 0 && !mac_is_multicast(addr2) &&
      !mac_is_zero_or_ff(addr2)) {
    post_hit(addr2, rssi, ch, METH_OUI_TX, PROTO_WIFI, conf2, nullptr);
  }
}

// ---------------------------------------------------------------------------
// Channel plan
// ---------------------------------------------------------------------------
void radio_set_channel_plan(uint8_t plan) {
  switch (plan) {
    case CHAN_PLAN_FULL:
      s_ch_tbl = CH_FULL;
      s_ch_count = sizeof(CH_FULL);
      break;
    case CHAN_PLAN_ASC:
      s_ch_tbl = CH_ASC;
      s_ch_count = sizeof(CH_ASC);
      break;
    default:
      s_ch_tbl = CH_PRIMARY;
      s_ch_count = sizeof(CH_PRIMARY);
      break;
  }
  s_ch_idx = 0;
  settings().chan_plan = plan;
}

static void hop_to(uint8_t ch) {
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  stats().current_channel = ch;
}

// ---------------------------------------------------------------------------
// WiFi sniffer helpers (re-used after BLE tear-down)
// ---------------------------------------------------------------------------
static void wifi_apply_sniffer_config() {
  wifi_promiscuous_filter_t filt = {};
  filt.filter_mask =
      WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_cb);
  esp_wifi_set_ps(WIFI_PS_NONE);
}

static void wifi_pause_for_ble() {
  esp_wifi_set_promiscuous(false);
  delay(20);
  // Full stop — classic ESP32 cannot keep the WiFi driver alive while NimBLE
  // owns the dual-mode controller; leaving it running causes delayed panics.
  esp_err_t e = esp_wifi_stop();
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
    Serial.printf("[wifi] stop for BLE: %s\n", esp_err_to_name(e));
  }
  s_wifi_started = false;
  delay(80);
}

static bool wifi_resume_after_ble() {
  esp_err_t e = esp_wifi_start();
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
    Serial.printf("[wifi] resume start failed: %s\n", esp_err_to_name(e));
    return false;
  }
  s_wifi_started = true;
  delay(40);
  wifi_apply_sniffer_config();
  if (s_scanning) {
    hop_to(s_ch_tbl[s_ch_idx]);
    e = esp_wifi_set_promiscuous(true);
    if (e != ESP_OK) {
      Serial.printf("[wifi] resume promisc failed: %s\n", esp_err_to_name(e));
      return false;
    }
    hop_to(s_ch_tbl[s_ch_idx]);
  }
  return true;
}

// ---------------------------------------------------------------------------
// BLE (optional, time-shared) — classic ESP32 safe path
//
// Each slice:  STOP WiFi → init NimBLE → blocking scan → deinit NimBLE →
//              START WiFi. No async callbacks (they race the radio handoff).
// ---------------------------------------------------------------------------
#if GFY_ENABLE_BLE

static bool s_ble_ready = false;
static bool s_ble_failed = false;
static bool s_bt_classic_released = false;

static void ble_parse_device(NimBLEAdvertisedDevice *dev) {
  if (!dev) return;
  Stats &st = stats();
  st.ble_adv++;

  uint8_t mac[6] = { 0 };
  const uint8_t *np = dev->getAddress().getNative();
  if (np) {
    // NimBLE stores BD_ADDR reversed; flip to network/MAC display order
    for (int i = 0; i < 6; i++) mac[i] = np[5 - i];
  }

  const int8_t rssi = (int8_t)dev->getRSSI();
  const uint8_t ch = stats().current_channel;

  if (dev->haveManufacturerData()) {
    std::string md = dev->getManufacturerData();
    if (md.size() >= 2) {
      uint16_t cid = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
      if (cid == BLE_MFG_XUNTONG) {
        post_hit(mac, rssi, ch, METH_BLE_MFG, PROTO_BLE, -1, "XUNTONG");
        return;
      }
    }
  }

  int conf = oui_match(mac);
  if (conf >= 0) {
    post_hit(mac, rssi, ch, METH_BLE_OUI, PROTO_BLE, conf, nullptr);
    return;
  }

  if (dev->haveName()) {
    std::string nm = dev->getName();
    char buf[32];
    size_t n = nm.size();
    if (n > 31) n = 31;
    for (size_t i = 0; i < n; i++)
      buf[i] = (char)tolower((unsigned char)nm[i]);
    buf[n] = 0;
    for (size_t k = 0; k < BLE_NAME_KEYWORD_COUNT; k++) {
      if (strstr(buf, BLE_NAME_KEYWORDS[k])) {
        char lab[20];
        strncpy(lab, nm.c_str(), sizeof(lab) - 1);
        lab[sizeof(lab) - 1] = 0;
        post_hit(mac, rssi, ch, METH_BLE_NAME, PROTO_BLE, -1, lab);
        return;
      }
    }
  }

  if (dev->haveServiceUUID()) {
    NimBLEUUID u = dev->getServiceUUID();
    if (u.bitSize() == 16) {
      const ble_uuid_any_t *native = u.getNative();
      uint16_t u16 = native ? native->u16.value : 0;
      for (size_t i = 0; i < RAVEN_SERVICE_UUID_COUNT; i++) {
        uint16_t base = RAVEN_SERVICE_UUIDS[i];
        if (u16 == base ||
            ((u16 & 0xFF00) == (base & 0xFF00) && (u16 & 0xFF00) >= 0x3100 &&
             (u16 & 0xFF00) <= 0x3500)) {
          post_hit(mac, rssi, ch, METH_BLE_UUID, PROTO_BLE, -1, "Raven");
          return;
        }
      }
    }
  }
}

static void ble_shutdown() {
  if (!s_ble_ready) {
    s_in_ble = false;
    return;
  }
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan) {
    if (scan->isScanning()) scan->stop();
    scan->clearResults();
  }
  // Tear down the entire stack so WiFi gets the controller back cleanly
  NimBLEDevice::deinit(true);
  s_ble_ready = false;
  s_in_ble = false;
  delay(50);
}

static bool ble_bringup() {
  if (s_ble_failed) return false;

  const uint32_t heap = ESP.getFreeHeap();
  if (heap < 52000) {
    Serial.printf("[ble] abort: low heap %u\n", (unsigned)heap);
    s_ble_failed = true;
    settings().ble_scan = false;
    return false;
  }

  // Free classic BT memory once (BLE-only) — reclaim ~30–60 KB on ESP32
  if (!s_bt_classic_released) {
    esp_err_t br = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    Serial.printf("[ble] classic BT mem release: %s\n", esp_err_to_name(br));
    s_bt_classic_released = true;
  }

  Serial.printf("[ble] init (heap=%u)...\n", (unsigned)ESP.getFreeHeap());
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_N0);

  NimBLEScan *scan = NimBLEDevice::getScan();
  if (!scan) {
    Serial.println(F("[ble] getScan failed"));
    NimBLEDevice::deinit(true);
    s_ble_failed = true;
    settings().ble_scan = false;
    return false;
  }

  // No callbacks — results collected after blocking start() returns
  scan->setAdvertisedDeviceCallbacks(nullptr);
  scan->setActiveScan(true);
  scan->setInterval(160);
  scan->setWindow(80);
  scan->setMaxResults(24);
  scan->setDuplicateFilter(true);
  s_ble_ready = true;
  Serial.printf("[ble] ready heap=%u\n", (unsigned)ESP.getFreeHeap());
  return true;
}

/** One finite BLE window. Always restores WiFi afterward. */
static void ble_run_slice() {
  if (s_ble_failed || !settings().ble_scan || s_in_ble) return;

  Serial.printf("[ble] slice begin heap=%u\n", (unsigned)ESP.getFreeHeap());
  s_in_ble = true;

  // 1) Stop WiFi driver completely
  wifi_pause_for_ble();

  // 2) BLE bring-up + blocking scan
  bool ok = ble_bringup();
  if (ok) {
    NimBLEScan *scan = NimBLEDevice::getScan();
    const uint32_t scan_sec =
        (BLE_SCAN_MS + 999UL) / 1000UL < 1 ? 1 : (BLE_SCAN_MS + 999UL) / 1000UL;
    Serial.printf("[ble] scanning %lus...\n", (unsigned long)scan_sec);

    // Blocking overload: duration in seconds on NimBLE 1.4.x
    NimBLEScanResults results = scan->start(scan_sec, false);
    const int n = results.getCount();
    Serial.printf("[ble] found %d devices\n", n);
    for (int i = 0; i < n; i++) {
      // Copy by value — pointer from getDevice is temporary
      NimBLEAdvertisedDevice dev = results.getDevice(i);
      ble_parse_device(&dev);
    }
    scan->clearResults();
  }

  // 3) Fully tear down BLE before WiFi returns
  ble_shutdown();

  // 4) Restart WiFi sniffer
  if (!wifi_resume_after_ble()) {
    Serial.println(F("[ble] WiFi resume failed — disabling BLE"));
    s_ble_failed = true;
    settings().ble_scan = false;
    // Last-ditch: try again
    delay(100);
    wifi_resume_after_ble();
  }

  s_in_ble = false;
  s_slice_start = millis();
  s_last_hop = millis();
  Serial.printf("[ble] slice end heap=%u ch=%u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)stats().current_channel);
}

static void ble_force_stop() {
  if (s_in_ble || s_ble_ready) {
    ble_shutdown();
  }
  s_in_ble = false;
}

#endif  // GFY_ENABLE_BLE

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void radio_init() {
  radio_set_channel_plan(settings().chan_plan);

  esp_err_t nvs = nvs_flash_init();
  if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(50);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t e = esp_wifi_init(&cfg);
  if (e != ESP_OK) {
    Serial.printf("[wifi] init failed: %s\n", esp_err_to_name(e));
    stats().radio_ok = false;
    return;
  }

  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  // STA + disconnected is more stable with occasional BLE on classic ESP32
  // than WIFI_MODE_NULL for stop/start cycles.
  e = esp_wifi_set_mode(WIFI_MODE_STA);
  if (e != ESP_OK) {
    Serial.printf("[wifi] set_mode(STA) failed: %s\n", esp_err_to_name(e));
  }
  WiFi.disconnect(false, false);

  e = esp_wifi_start();
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
    Serial.printf("[wifi] start failed: %s\n", esp_err_to_name(e));
  } else {
    s_wifi_started = true;
  }

  wifi_apply_sniffer_config();
  esp_wifi_set_promiscuous(false);
  esp_log_level_set("wifi", ESP_LOG_ERROR);
  esp_log_level_set("bt", ESP_LOG_ERROR);
  esp_log_level_set("NimBLE", ESP_LOG_ERROR);

  stats().radio_ok = true;
  s_scanning = false;
  Serial.println(F("[wifi] promiscuous stack ready (STA disc.)"));
}

void radio_start() {
  if (s_scanning) return;
  if (!s_wifi_started) {
    if (!wifi_resume_after_ble()) {
      Serial.println(F("[wifi] cannot start — wifi down"));
      return;
    }
  }
  hop_to(s_ch_tbl[s_ch_idx]);
  esp_err_t e = esp_wifi_set_promiscuous(true);
  if (e != ESP_OK) {
    Serial.printf("[wifi] promisc on failed: %s\n", esp_err_to_name(e));
    return;
  }
  delay(30);
  hop_to(s_ch_tbl[s_ch_idx]);
  s_scanning = true;
  s_last_hop = millis();
  s_slice_start = millis();
  s_in_ble = false;
  settings().scanning = true;
  Serial.printf("[wifi] SCAN start plan=%u dwell=%ums ch=%u\n",
                (unsigned)settings().chan_plan,
                (unsigned)settings().dwell_ms,
                (unsigned)s_ch_tbl[s_ch_idx]);
}

void radio_stop() {
  if (!s_scanning) return;
#if GFY_ENABLE_BLE
  ble_force_stop();
  if (!s_wifi_started) {
    wifi_resume_after_ble();
  }
#endif
  esp_wifi_set_promiscuous(false);
  s_scanning = false;
  settings().scanning = false;
  Serial.println(F("[wifi] SCAN stopped"));
}

bool radio_is_scanning() { return s_scanning; }

uint8_t radio_current_channel() { return stats().current_channel; }

void radio_ble_setting_changed(bool enabled) {
#if GFY_ENABLE_BLE
  s_slice_start = millis();
  if (!enabled) {
    ble_force_stop();
    if (!s_wifi_started) {
      wifi_resume_after_ble();
    } else if (s_scanning) {
      esp_wifi_set_promiscuous(true);
      hop_to(s_ch_tbl[s_ch_idx]);
    }
  } else {
    s_ble_failed = false;
    // Give a full WiFi window before first BLE handoff
    s_slice_start = millis();
    Serial.println(F("[ble] enabled — first slice after WiFi period"));
  }
#else
  (void)enabled;
#endif
}

void radio_tick(uint32_t now) {
  if (!s_scanning) return;
  if (s_in_ble) return;  // shouldn't happen (blocking), but guard

#if GFY_ENABLE_BLE
  if (settings().ble_scan && !s_ble_failed) {
    if (now - s_slice_start >= WIFI_SLICE_MS) {
      ble_run_slice();
      return;
    }
  }
#endif

  const uint16_t dwell = settings().dwell_ms;
  if (now - s_last_hop >= dwell) {
    s_last_hop = now;
    s_ch_idx = (s_ch_idx + 1) % s_ch_count;
    hop_to(s_ch_tbl[s_ch_idx]);
  }
}

}  // namespace gfy
