/**
 * GoFlockYourself — polished 240×320 dark UI
 *
 * Screens: Home / Menu / Methods / Alerts / History / About / Alert flash
 * Touch via XPT2046 on dedicated VSPI pins.
 * Keep draws cheap: dirty-region style full redraws on interaction only,
 * lightweight status refresh while scanning.
 */
#include "ui.h"
#include "config.h"
#include "detection.h"
#include "radio.h"
#include "logger.h"
#include "hardware.h"
#include "oui_list.h"

// logger_sd_* used for SD status on home / alerts

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <Preferences.h>
#include <string.h>

namespace gfy {

// ---------------------------------------------------------------------------
// Palette — dark, high-contrast, field-readable
// ---------------------------------------------------------------------------
static const uint16_t C_BG      = 0x0841;  // near-black blue
static const uint16_t C_PANEL   = 0x10A2;  // dark panel
static const uint16_t C_BORDER  = 0x2A49;  // muted border
static const uint16_t C_TEXT    = 0xEF5D;  // soft white
static const uint16_t C_DIM     = 0x8410;  // secondary text
static const uint16_t C_ACCENT  = 0x07FF;  // cyan
static const uint16_t C_WARN    = 0xFD20;  // amber
static const uint16_t C_ALERT   = 0xF800;  // red
static const uint16_t C_OK      = 0x07E0;  // green
static const uint16_t C_TITLE   = 0x5E7F;  // light blue
static const uint16_t C_BTN     = 0x1A6E;
static const uint16_t C_BTN_ON  = 0x054B;
static const uint16_t C_HIT_BG  = 0x5000;  // deep red for alert

// ---------------------------------------------------------------------------
// Display / touch
// ---------------------------------------------------------------------------
static TFT_eSPI tft;
static SPIClass touchSPI(VSPI);
static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

static UiScreen s_screen = SCR_HOME;
static UiScreen s_prev_screen = SCR_HOME;
static bool s_need_full = true;
static uint32_t s_last_status = 0;
static uint32_t s_alert_until = 0;
static DetectEvent s_alert_ev;
static uint32_t s_touch_guard = 0;
static uint32_t s_ignore_touch_until = 0;  // boot / post-SD ignore window
static uint8_t s_hist_page = 0;
static uint8_t s_alert_phase = 0;
static uint32_t s_alert_anim = 0;

static void apply_invert() {
  tft.invertDisplay(settings().invert_display);
}

static void save_invert() {
  Preferences p;
  if (!p.begin("gfy", false)) return;
  p.putBool("inv", settings().invert_display);
  p.putBool("invset", true);
  p.end();
}

static bool invert_calibrated() {
  Preferences p;
  if (!p.begin("gfy", true)) return false;
  bool set = p.getBool("invset", false);
  p.end();
  return set;
}

static void load_invert() {
  Preferences p;
  if (!p.begin("gfy", true)) return;
  if (p.getBool("invset", false)) {
    settings().invert_display = p.getBool("inv", DEF_INVERT_DISPLAY);
  }
  p.end();
}

static void toggle_invert() {
  settings().invert_display = !settings().invert_display;
  apply_invert();
  save_invert();
  Serial.printf("[ui] invert display %s\n",
                settings().invert_display ? "ON" : "OFF");
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------
static void fill_screen(uint16_t c) { tft.fillScreen(c); }

static void panel(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fill = C_PANEL) {
  tft.fillRoundRect(x, y, w, h, 6, fill);
  tft.drawRoundRect(x, y, w, h, 6, C_BORDER);
}

static void header_bar(const char *title, bool back = false) {
  tft.fillRect(0, 0, TFT_W, 28, C_PANEL);
  tft.drawFastHLine(0, 28, TFT_W, C_BORDER);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_TITLE, C_PANEL);
  tft.drawString(title, 8, 14, 2);
  if (back) {
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(C_ACCENT, C_PANEL);
    tft.drawString("< BACK", TFT_W - 8, 14, 2);
  }
}

static void btn(int16_t x, int16_t y, int16_t w, int16_t h,
                const char *label, bool on = false, bool danger = false) {
  uint16_t bg = danger ? 0x8000 : (on ? C_BTN_ON : C_BTN);
  uint16_t fg = danger ? C_ALERT : (on ? C_OK : C_TEXT);
  tft.fillRoundRect(x, y, w, h, 5, bg);
  tft.drawRoundRect(x, y, w, h, 5, on ? C_OK : C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bg);
  tft.drawString(label, x + w / 2, y + h / 2, 2);
}

static void label_value(int16_t x, int16_t y, const char *lab, const char *val,
                        uint16_t vcol = C_TEXT) {
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString(lab, x, y, 1);
  tft.setTextColor(vcol, C_PANEL);
  tft.drawString(val, x + 78, y, 1);
}

static void fmt_uptime(char *buf, size_t n) {
  uint32_t s = millis() / 1000;
  uint32_t m = s / 60;
  uint32_t h = m / 60;
  s %= 60;
  m %= 60;
  if (h > 0) snprintf(buf, n, "%luh %02lum", (unsigned long)h, (unsigned long)m);
  else       snprintf(buf, n, "%lum %02lus", (unsigned long)m, (unsigned long)s);
}

static void fmt_ago(char *buf, size_t n, uint32_t then) {
  if (then == 0) { snprintf(buf, n, "—"); return; }
  uint32_t sec = (millis() - then) / 1000;
  if (sec < 60) snprintf(buf, n, "%lus ago", (unsigned long)sec);
  else if (sec < 3600) snprintf(buf, n, "%lum ago", (unsigned long)(sec / 60));
  else snprintf(buf, n, "%luh ago", (unsigned long)(sec / 3600));
}

// ---------------------------------------------------------------------------
// Touch mapping
// ---------------------------------------------------------------------------
struct TouchPt { int16_t x, y; bool pressed; };

static TouchPt read_touch() {
  TouchPt p = { 0, 0, false };
  // IRQ first — avoids SPI transactions when the panel is idle
  if (!ts.tirqTouched()) return p;
  if (!ts.touched()) return p;
  TS_Point t = ts.getPoint();
  if (t.z < TOUCH_Z_THRESHOLD) return p;

  // Map raw → screen (calibrate via TOUCH_* in config.h if offset)
  int16_t x = map(t.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, TFT_W - 1);
  int16_t y = map(t.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, TFT_H - 1);
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= TFT_W) x = TFT_W - 1;
  if (y >= TFT_H) y = TFT_H - 1;
  p.x = x;
  p.y = y;
  p.pressed = true;
  return p;
}

static bool hit(int16_t x, int16_t y, int16_t bx, int16_t by, int16_t bw, int16_t bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

// ---------------------------------------------------------------------------
// HOME screen
// ---------------------------------------------------------------------------
static void draw_home_full() {
  fill_screen(C_BG);

  // Title
  tft.fillRect(0, 0, TFT_W, 36, C_PANEL);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_ACCENT, C_PANEL);
  tft.drawString("GoFlockYourself", 8, 12, 2);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString("v" GFY_VERSION, 8, 28, 1);
  tft.setTextDatum(MR_DATUM);
  Settings &cfg = settings();
  tft.setTextColor(cfg.scanning ? C_OK : C_WARN, C_PANEL);
  tft.drawString(cfg.scanning ? "LIVE" : "IDLE", TFT_W - 8, 18, 2);
  tft.drawFastHLine(0, 36, TFT_W, C_BORDER);

  // Status card
  panel(6, 44, TFT_W - 12, 72);
  char buf[40];
  snprintf(buf, sizeof(buf), "CH %u", (unsigned)stats().current_channel);
  label_value(14, 58, "Channel", buf, C_ACCENT);
  label_value(14, 74, "Mode",
              cfg.chan_plan == CHAN_PLAN_FULL ? "Full 1-13" :
              cfg.chan_plan == CHAN_PLAN_ASC  ? "Asc 1-11" : "Primary 1/6/11",
              C_TEXT);
  snprintf(buf, sizeof(buf), "%s%s%s",
           cfg.wifi_oui_tx ? "OUI " : "",
           cfg.wildcard_probe ? "WILD " : "",
           cfg.ble_scan ? "BLE" : "");
  if (!buf[0]) strcpy(buf, "—");
  label_value(14, 90, "Methods", buf, C_TEXT);
  {
    const char *sd = logger_sd_status();
    char frames[24];
    snprintf(frames, sizeof(frames), "%lu", (unsigned long)stats().rx_frames);
    // Show frames + SD status on last status row
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_DIM, C_PANEL);
    tft.drawString("Frames", 14, 106, 1);
    tft.setTextColor(C_DIM, C_PANEL);
    tft.drawString(frames, 92, 106, 1);
    tft.setTextDatum(MR_DATUM);
    uint16_t sdcol = C_DIM;
    if (!strcmp(sd, "ready")) sdcol = C_OK;
    else if (!strcmp(sd, "format") || !strcmp(sd, "error")) sdcol = C_ALERT;
    char sdbuf[20];
    snprintf(sdbuf, sizeof(sdbuf), "SD:%s", sd);
    tft.setTextColor(sdcol, C_PANEL);
    tft.drawString(sdbuf, TFT_W - 14, 106, 1);
  }

  // Counters
  panel(6, 124, 110, 56);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString("HITS", 61, 138, 1);
  tft.setTextColor(stats().hits_total ? C_WARN : C_TEXT, C_PANEL);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats().hits_total);
  tft.drawString(buf, 61, 158, 4);

  panel(124, 124, 110, 56);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString("UNIQUE", 179, 138, 1);
  tft.setTextColor(stats().hits_unique ? C_ALERT : C_TEXT, C_PANEL);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats().hits_unique);
  tft.drawString(buf, 179, 158, 4);

  // Last detection
  panel(6, 188, TFT_W - 12, 72);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString("LAST DETECTION", 14, 200, 1);

  DetectEvent last;
  uint32_t last_ms = 0;
  if (last_hit(last, last_ms)) {
    char macs[18], ouis[9], ago[16];
    oui_to_str(last.mac, ouis, sizeof(ouis));
    mac_to_str(last.mac, macs, sizeof(macs));
    fmt_ago(ago, sizeof(ago), last_ms);
    tft.setTextColor(C_WARN, C_PANEL);
    tft.drawString(method_name(last.method), 14, 216, 2);
    tft.setTextColor(C_TEXT, C_PANEL);
    snprintf(buf, sizeof(buf), "%s  %ddBm  ch%u", ouis, (int)last.rssi, last.channel);
    tft.drawString(buf, 14, 234, 1);
    tft.setTextColor(C_DIM, C_PANEL);
    tft.drawString(ago, 14, 248, 1);
  } else {
    tft.setTextColor(C_DIM, C_PANEL);
    tft.drawString("No detections yet", 14, 224, 2);
  }

  // Footer stats
  panel(6, 268, TFT_W - 12, 44);
  char up[20];
  fmt_uptime(up, sizeof(up));
  snprintf(buf, sizeof(buf), "Up %s", up);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString(buf, 14, 282, 1);
  snprintf(buf, sizeof(buf), "Heap %u", (unsigned)ESP.getFreeHeap());
  tft.drawString(buf, 14, 298, 1);
  snprintf(buf, sizeof(buf), "P %lu  W %lu",
           (unsigned long)stats().rx_probe,
           (unsigned long)stats().rx_wildcard);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(buf, TFT_W - 14, 282, 1);
  tft.setTextColor(C_ACCENT, C_PANEL);
  tft.drawString("MENU >", TFT_W - 14, 298, 1);
}

static void refresh_home_status() {
  // Lightweight update of dynamic fields without full redraw
  char buf[40];
  Settings &cfg = settings();

  // LIVE / IDLE + channel
  tft.fillRect(160, 4, 76, 28, C_PANEL);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(cfg.scanning ? C_OK : C_WARN, C_PANEL);
  tft.drawString(cfg.scanning ? "LIVE" : "IDLE", TFT_W - 8, 18, 2);

  tft.fillRect(90, 50, 130, 14, C_PANEL);
  tft.setTextDatum(ML_DATUM);
  snprintf(buf, sizeof(buf), "CH %u", (unsigned)stats().current_channel);
  tft.setTextColor(C_ACCENT, C_PANEL);
  tft.drawString(buf, 92, 58, 1);

  tft.fillRect(90, 98, 70, 14, C_PANEL);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats().rx_frames);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString(buf, 92, 106, 1);
  {
    const char *sd = logger_sd_status();
    tft.fillRect(160, 98, 70, 14, C_PANEL);
    uint16_t sdcol = C_DIM;
    if (!strcmp(sd, "ready")) sdcol = C_OK;
    else if (!strcmp(sd, "format") || !strcmp(sd, "error")) sdcol = C_ALERT;
    char sdbuf[20];
    snprintf(sdbuf, sizeof(sdbuf), "SD:%s", sd);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(sdcol, C_PANEL);
    tft.drawString(sdbuf, TFT_W - 14, 106, 1);
  }

  // Hit counters
  tft.fillRect(16, 146, 90, 28, C_PANEL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(stats().hits_total ? C_WARN : C_TEXT, C_PANEL);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats().hits_total);
  tft.drawString(buf, 61, 158, 4);

  tft.fillRect(134, 146, 90, 28, C_PANEL);
  tft.setTextColor(stats().hits_unique ? C_ALERT : C_TEXT, C_PANEL);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats().hits_unique);
  tft.drawString(buf, 179, 158, 4);

  // Footer heap/uptime
  tft.fillRect(12, 274, 140, 32, C_PANEL);
  char up[20];
  fmt_uptime(up, sizeof(up));
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_DIM, C_PANEL);
  snprintf(buf, sizeof(buf), "Up %s", up);
  tft.drawString(buf, 14, 282, 1);
  snprintf(buf, sizeof(buf), "Heap %u", (unsigned)ESP.getFreeHeap());
  tft.drawString(buf, 14, 298, 1);

  // Last detection ago refresh
  DetectEvent last;
  uint32_t last_ms = 0;
  if (last_hit(last, last_ms)) {
    tft.fillRect(12, 240, 100, 16, C_PANEL);
    char ago[16];
    fmt_ago(ago, sizeof(ago), last_ms);
    tft.setTextColor(C_DIM, C_PANEL);
    tft.drawString(ago, 14, 248, 1);
  }
}

// ---------------------------------------------------------------------------
// MENU
// ---------------------------------------------------------------------------
static void draw_menu() {
  Serial.println(F("[ui] draw MAIN MENU"));
  fill_screen(C_BG);
  header_bar("MAIN MENU", true);

  Settings &cfg = settings();
  const char *scan_lab = cfg.scanning ? "Stop Scanning" : "Start Scanning";
  btn(16, 44, TFT_W - 32, 36, scan_lab, cfg.scanning, cfg.scanning);

  btn(16, 90,  TFT_W - 32, 36, "Detection Methods");
  btn(16, 136, TFT_W - 32, 36, "Alerts & Sensitivity");
  btn(16, 182, TFT_W - 32, 36, "View Log / History");
  btn(16, 228, TFT_W - 32, 36, "About / Credits");

  char invlab[28];
  snprintf(invlab, sizeof(invlab), "Invert display  %s",
           cfg.invert_display ? "ON" : "OFF");
  btn(16, 274, TFT_W - 32, 36, invlab, cfg.invert_display);
  Serial.println(F("[ui] MAIN MENU ready"));
}

// ---------------------------------------------------------------------------
// METHODS
// ---------------------------------------------------------------------------
static void draw_methods() {
  fill_screen(C_BG);
  header_bar("DETECTION METHODS", true);
  Settings &cfg = settings();

  auto row = [&](int16_t y, const char *name, bool on) {
    panel(8, y, TFT_W - 16, 34);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_TEXT, C_PANEL);
    tft.drawString(name, 16, y + 17, 2);
    btn(TFT_W - 78, y + 5, 60, 24, on ? "ON" : "OFF", on);
  };

  row(40,  "WiFi OUI (TX)",     cfg.wifi_oui_tx);
  row(80,  "WiFi OUI (RX)",     cfg.wifi_oui_rx);
  row(120, "Wildcard Probe",    cfg.wildcard_probe);
  row(160, "Broad OUI",         cfg.broad_oui);
  row(200, "SSID Keywords",     cfg.ssid_keywords);
#if GFY_ENABLE_BLE
  row(240, "BLE Scanning",      cfg.ble_scan);
#else
  panel(8, 240, TFT_W - 16, 34);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString("BLE (not built)", 16, 257, 2);
#endif

  // Channel plan strip
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  char buf[48];
  snprintf(buf, sizeof(buf), "Plan: %s  dwell %ums",
           cfg.chan_plan == CHAN_PLAN_FULL ? "Full" :
           cfg.chan_plan == CHAN_PLAN_ASC  ? "Asc" : "1/6/11",
           (unsigned)cfg.dwell_ms);
  tft.drawString(buf, TFT_W / 2, 300, 1);
}

// ---------------------------------------------------------------------------
// ALERTS / SENSITIVITY
// ---------------------------------------------------------------------------
static void draw_alerts_cfg() {
  fill_screen(C_BG);
  header_bar("ALERTS & SENS", true);
  Settings &cfg = settings();

  auto row = [&](int16_t y, const char *name, bool on) {
    panel(8, y, TFT_W - 16, 34);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_TEXT, C_PANEL);
    tft.drawString(name, 16, y + 17, 2);
    btn(TFT_W - 78, y + 5, 60, 24, on ? "ON" : "OFF", on);
  };

  row(40,  "Audio (buzzer)", cfg.audio_alert);
  row(80,  "RGB LED alert",  cfg.led_alert);
  row(120, "SD logging",     cfg.sd_logging);
  row(160, "Invert display", cfg.invert_display);

  // Channel plan buttons
  panel(8, 204, TFT_W - 16, 70);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString("CHANNEL PLAN", 16, 218, 1);
  btn(16,  232, 64, 28, "1/6/11", cfg.chan_plan == CHAN_PLAN_PRIMARY);
  btn(88,  232, 64, 28, "Full",   cfg.chan_plan == CHAN_PLAN_FULL);
  btn(160, 232, 64, 28, "Asc",    cfg.chan_plan == CHAN_PLAN_ASC);

  const char *st = logger_sd_status();
  uint16_t sdcol = C_DIM;
  if (!strcmp(st, "ready")) sdcol = C_OK;
  else if (!strcmp(st, "format") || !strcmp(st, "error")) sdcol = C_ALERT;
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(sdcol, C_BG);
  tft.drawString(logger_sd_detail(), TFT_W / 2, 284, 1);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("Logging OK while scanning", TFT_W / 2, 304, 1);
}

// ---------------------------------------------------------------------------
// HISTORY
// ---------------------------------------------------------------------------
static void draw_history() {
  fill_screen(C_BG);
  header_bar("HISTORY", true);

  size_t n = history_count();
  char buf[40];
  snprintf(buf, sizeof(buf), "%u stored", (unsigned)n);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString(buf, TFT_W - 70, 14, 1);

  if (n == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("No detections yet", TFT_W / 2, 160, 2);
    return;
  }

  const size_t per_page = 6;
  size_t pages = (n + per_page - 1) / per_page;
  if (s_hist_page >= pages) s_hist_page = 0;
  size_t start = s_hist_page * per_page;

  for (size_t i = 0; i < per_page; i++) {
    const HistoryEntry *e = history_get(start + i);
    if (!e) break;
    int16_t y = 36 + (int16_t)i * 40;
    panel(6, y, TFT_W - 12, 36);
    char macs[18], ago[16];
    mac_to_str(e->mac, macs, sizeof(macs));
    fmt_ago(ago, sizeof(ago), e->millis_at);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_WARN, C_PANEL);
    tft.drawString(method_name(e->method), 12, y + 12, 1);
    tft.setTextColor(C_TEXT, C_PANEL);
    tft.drawString(macs, 12, y + 26, 1);
    tft.setTextDatum(MR_DATUM);
    snprintf(buf, sizeof(buf), "%ddBm", (int)e->rssi);
    tft.setTextColor(C_ACCENT, C_PANEL);
    tft.drawString(buf, TFT_W - 12, y + 12, 1);
    tft.setTextColor(C_DIM, C_PANEL);
    tft.drawString(ago, TFT_W - 12, y + 26, 1);
  }

  // Page controls — hide Prev on first page, Next on last page
  if (pages > 1) {
    if (s_hist_page > 0) {
      btn(16, 286, 90, 28, "< Prev");
    }
    if (s_hist_page + 1 < pages) {
      btn(TFT_W - 106, 286, 90, 28, "Next >");
    }
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_DIM, C_BG);
    snprintf(buf, sizeof(buf), "%u/%u", (unsigned)(s_hist_page + 1), (unsigned)pages);
    tft.drawString(buf, TFT_W / 2, 300, 1);
  }
}

// ---------------------------------------------------------------------------
// ABOUT
// ---------------------------------------------------------------------------
static void draw_about() {
  fill_screen(C_BG);
  header_bar("ABOUT", true);

  panel(8, 40, TFT_W - 16, 200);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT, C_PANEL);
  tft.drawString("GoFlockYourself", TFT_W / 2, 60, 2);
  tft.setTextColor(C_TEXT, C_PANEL);
  tft.drawString("v" GFY_VERSION, TFT_W / 2, 80, 2);
  tft.setTextColor(C_WARN, C_PANEL);
  tft.drawString("By @OffGridPete", TFT_W / 2, 98, 2);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString("Passive Flock Safety detector", 18, 118, 1);
  tft.drawString("WiFi promiscuous + OUI + wild", 18, 133, 1);
  tft.drawString("probe + optional BLE", 18, 148, 1);
  tft.drawString(GFY_HW_NAME, 18, 165, 1);

  tft.setTextColor(C_TITLE, C_PANEL);
  tft.drawString("Research credits:", 18, 185, 1);
  tft.setTextColor(C_DIM, C_PANEL);
  tft.drawString("@NitekryDPaul  OUI list", 18, 200, 1);
  tft.drawString("DeFlockJoplin  wild probe", 18, 215, 1);
  tft.drawString("colonelpanichacks  flock-you", 18, 230, 1);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("Receive-only. Field research tool.", TFT_W / 2, 270, 1);
  char buf[48];
  snprintf(buf, sizeof(buf), "OUIs loaded: %u", (unsigned)FLOCK_OUI_COUNT);
  tft.drawString(buf, TFT_W / 2, 290, 1);
}

// ---------------------------------------------------------------------------
// ALERT full-screen
// ---------------------------------------------------------------------------
static void draw_alert_frame(bool flash_on) {
  uint16_t bg = flash_on ? C_HIT_BG : C_BG;
  fill_screen(bg);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(flash_on ? C_TEXT : C_ALERT, bg);
  tft.drawString("!! DETECTION !!", TFT_W / 2, 40, 4);

  tft.setTextColor(C_WARN, bg);
  tft.drawString(method_name(s_alert_ev.method), TFT_W / 2, 90, 2);

  char macs[18], ouis[9], buf[48];
  mac_to_str(s_alert_ev.mac, macs, sizeof(macs));
  oui_to_str(s_alert_ev.mac, ouis, sizeof(ouis));

  panel(12, 115, TFT_W - 24, 120, flash_on ? 0x2800 : C_PANEL);
  tft.setTextColor(C_TEXT, flash_on ? 0x2800 : C_PANEL);
  tft.drawString(macs, TFT_W / 2, 140, 2);
  snprintf(buf, sizeof(buf), "OUI %s", ouis);
  tft.setTextColor(C_ACCENT, flash_on ? 0x2800 : C_PANEL);
  tft.drawString(buf, TFT_W / 2, 165, 2);
  snprintf(buf, sizeof(buf), "RSSI %d dBm   CH %u",
           (int)s_alert_ev.rssi, (unsigned)s_alert_ev.channel);
  tft.setTextColor(C_TEXT, flash_on ? 0x2800 : C_PANEL);
  tft.drawString(buf, TFT_W / 2, 190, 2);
  snprintf(buf, sizeof(buf), "Confidence %u%%", (unsigned)s_alert_ev.confidence);
  tft.setTextColor(C_WARN, flash_on ? 0x2800 : C_PANEL);
  tft.drawString(buf, TFT_W / 2, 215, 2);

  if (s_alert_ev.label[0]) {
    tft.setTextColor(C_DIM, bg);
    tft.drawString(s_alert_ev.label, TFT_W / 2, 260, 2);
  }

  tft.setTextColor(C_DIM, bg);
  tft.drawString("Tap to dismiss", TFT_W / 2, 300, 1);
}

// ---------------------------------------------------------------------------
// Screen switcher
// ---------------------------------------------------------------------------
static void draw_screen() {
  switch (s_screen) {
    case SCR_HOME:       draw_home_full(); break;
    case SCR_MENU:       draw_menu(); break;
    case SCR_METHODS:    draw_methods(); break;
    case SCR_ALERTS_CFG: draw_alerts_cfg(); break;
    case SCR_HISTORY:    draw_history(); break;
    case SCR_ABOUT:      draw_about(); break;
    case SCR_ALERT:      draw_alert_frame(true); break;
  }
  s_need_full = false;
}

static void go(UiScreen s) {
  if (s_screen != SCR_ALERT) s_prev_screen = s_screen;
  s_screen = s;
  s_need_full = true;
}

// ---------------------------------------------------------------------------
// Touch handlers
// ---------------------------------------------------------------------------
static void handle_touch(int16_t x, int16_t y) {
  Settings &cfg = settings();

  // Global back zone on non-home screens
  if (s_screen != SCR_HOME && s_screen != SCR_ALERT) {
    if (hit(x, y, TFT_W - 90, 0, 90, 32)) {
      go(s_screen == SCR_MENU ? SCR_HOME : SCR_MENU);
      return;
    }
  }

  switch (s_screen) {
    case SCR_HOME:
      // Explicit zones only (avoid full-screen accidental opens)
      if (y > 268 || hit(x, y, TFT_W - 80, 0, 80, 36)) {
        Serial.println(F("[ui] open MAIN MENU"));
        go(SCR_MENU);
      }
      break;

    case SCR_MENU:
      if (hit(x, y, 16, 44, TFT_W - 32, 36)) {
        Serial.println(F("[ui] menu: toggle scan"));
        if (cfg.scanning) {
          radio_stop();
          led_status_idle();
        } else {
          radio_start();
          led_status_scanning();
        }
        s_need_full = true;
      } else if (hit(x, y, 16, 90, TFT_W - 32, 36)) {
        Serial.println(F("[ui] menu: methods"));
        go(SCR_METHODS);
      } else if (hit(x, y, 16, 136, TFT_W - 32, 36)) {
        Serial.println(F("[ui] menu: alerts"));
        go(SCR_ALERTS_CFG);
      } else if (hit(x, y, 16, 182, TFT_W - 32, 36)) {
        Serial.println(F("[ui] menu: history"));
        s_hist_page = 0;
        go(SCR_HISTORY);
      } else if (hit(x, y, 16, 228, TFT_W - 32, 36)) {
        Serial.println(F("[ui] menu: about"));
        go(SCR_ABOUT);
      } else if (hit(x, y, 16, 274, TFT_W - 32, 36)) {
        Serial.println(F("[ui] menu: invert display"));
        toggle_invert();
        s_need_full = true;
      }
      break;

    case SCR_METHODS:
      // Toggle rows (tap right-side ON/OFF or whole row)
      if (hit(x, y, 8, 40,  TFT_W - 16, 34)) { cfg.wifi_oui_tx = !cfg.wifi_oui_tx; s_need_full = true; }
      else if (hit(x, y, 8, 80,  TFT_W - 16, 34)) { cfg.wifi_oui_rx = !cfg.wifi_oui_rx; s_need_full = true; }
      else if (hit(x, y, 8, 120, TFT_W - 16, 34)) { cfg.wildcard_probe = !cfg.wildcard_probe; s_need_full = true; }
      else if (hit(x, y, 8, 160, TFT_W - 16, 34)) { cfg.broad_oui = !cfg.broad_oui; s_need_full = true; }
      else if (hit(x, y, 8, 200, TFT_W - 16, 34)) { cfg.ssid_keywords = !cfg.ssid_keywords; s_need_full = true; }
#if GFY_ENABLE_BLE
      else if (hit(x, y, 8, 240, TFT_W - 16, 34)) {
        cfg.ble_scan = !cfg.ble_scan;
        radio_ble_setting_changed(cfg.ble_scan);
        s_need_full = true;
      }
#endif
      break;

    case SCR_ALERTS_CFG:
      if (hit(x, y, 8, 40,  TFT_W - 16, 34)) { cfg.audio_alert = !cfg.audio_alert; s_need_full = true; }
      else if (hit(x, y, 8, 80,  TFT_W - 16, 34)) { cfg.led_alert = !cfg.led_alert; s_need_full = true; }
      else if (hit(x, y, 8, 120, TFT_W - 16, 34)) {
        // Persist + schedule probe on logger_tick (SD.begin is not safe here)
        logger_set_enabled(!cfg.sd_logging);
        s_need_full = true;
      } else if (hit(x, y, 8, 160, TFT_W - 16, 34)) {
        toggle_invert();
        s_need_full = true;
      } else if (hit(x, y, 16, 232, 64, 28)) {
        radio_set_channel_plan(CHAN_PLAN_PRIMARY);
        s_need_full = true;
      } else if (hit(x, y, 88, 232, 64, 28)) {
        radio_set_channel_plan(CHAN_PLAN_FULL);
        s_need_full = true;
      } else if (hit(x, y, 160, 232, 64, 28)) {
        radio_set_channel_plan(CHAN_PLAN_ASC);
        s_need_full = true;
      }
      break;

    case SCR_HISTORY: {
      size_t n = history_count();
      size_t pages = n ? (n + 5) / 6 : 1;
      if (hit(x, y, 16, 286, 90, 28) && s_hist_page > 0) {
        s_hist_page--;
        s_need_full = true;
      } else if (hit(x, y, TFT_W - 106, 286, 90, 28) && s_hist_page + 1 < pages) {
        s_hist_page++;
        s_need_full = true;
      }
      break;
    }

    case SCR_ABOUT:
      break;

    case SCR_ALERT:
      s_alert_until = 0;
      go(s_prev_screen == SCR_ALERT ? SCR_HOME : s_prev_screen);
      if (settings().scanning) led_status_scanning();
      else led_status_idle();
      break;
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void ui_spi_release_for_sd() {
  // Deselect touch chip; logger will re-pin VSPI onto SD pads
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  // Release the bus so the reclaim-side begin() can actually re-pin it.
  // SPIClass::begin() returns early when _spi is already set, so without this
  // end() the reclaim below is a silent no-op and touch never comes back.
  touchSPI.end();
}

// Panel lots can't be detected from the controller: invert happens after
// GRAM, so readPixel() always returns what we wrote. First boot: show a
// pure black half and a pure white half; the user taps whichever LOOKS
// dark. That choice is stored in NVS for this board.
static void run_panel_setup() {
  tft.invertDisplay(false);
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 72, 120, 248, TFT_BLACK);
  tft.fillRect(120, 72, 120, 248, TFT_WHITE);
  tft.drawFastVLine(120, 72, 248, 0x8410);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("TAP THE DARK SIDE", TFT_W / 2, 22, 2);
  tft.setTextColor(0x8410, TFT_BLACK);
  tft.drawString("once per board — CYD panels differ", TFT_W / 2, 48, 1);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("A", 60, 180, 4);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString("B", 180, 180, 4);

  uint32_t start = millis();
  while (millis() - start < 500) {
    (void)read_touch();
    delay(20);
  }
  while (read_touch().pressed) delay(20);

  for (;;) {
    TouchPt t = read_touch();
    if (t.pressed && t.y > 72) {
      settings().invert_display = (t.x >= 120);
      apply_invert();
      save_invert();
      Serial.printf("[ui] panel setup invert=%s tap=(%d,%d)\n",
                    settings().invert_display ? "ON" : "OFF",
                    (int)t.x, (int)t.y);
      tft.fillScreen(C_BG);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_ACCENT, C_BG);
      tft.drawString("Display saved", TFT_W / 2, TFT_H / 2, 2);
      delay(450);
      while (read_touch().pressed) delay(20);
      return;
    }
    delay(20);
  }
}

void ui_spi_reclaim_after_sd() {
  // Put VSPI back on touch pins. Use the touch SPIClass only — do not call
  // ts.begin() again (that path has hung on some CYD boards).
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  delay(1);
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  delay(1);
  ts.setRotation(0);
  s_ignore_touch_until = millis() + 400;
}

void ui_init() {
  // Backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(0);  // portrait
  tft.setTextFont(2);

  // Touch on dedicated VSPI pins
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(0);

  if (invert_calibrated()) {
    load_invert();
    apply_invert();
  } else {
    run_panel_setup();
  }
  Serial.printf("[ui] invert display %s\n",
                settings().invert_display ? "ON" : "OFF");

  // Ignore phantom resistive presses after setup / splash
  s_ignore_touch_until = millis() + 1500;
  s_touch_guard = 0;

  // Splash
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("GoFlockYourself", TFT_W / 2, 120, 4);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("Passive Flock Detector", TFT_W / 2, 160, 2);
  tft.drawString("v" GFY_VERSION, TFT_W / 2, 185, 2);
  delay(700);

  s_screen = SCR_HOME;
  s_need_full = true;
}

void ui_show_alert(const DetectEvent &ev) {
  s_alert_ev = ev;
  s_alert_until = millis() + ALERT_DISPLAY_MS;
  s_alert_phase = 0;
  s_alert_anim = millis();
  go(SCR_ALERT);
  draw_alert_frame(true);
}

void ui_force_redraw() { s_need_full = true; }

UiScreen ui_screen() { return s_screen; }

void ui_tick(uint32_t now) {
  // Alert animation / timeout
  if (s_screen == SCR_ALERT) {
    if (now >= s_alert_until) {
      go(s_prev_screen == SCR_ALERT ? SCR_HOME : s_prev_screen);
      if (settings().scanning) led_status_scanning();
      else led_status_idle();
    } else if (now - s_alert_anim >= 180) {
      s_alert_anim = now;
      s_alert_phase ^= 1;
      draw_alert_frame(s_alert_phase != 0);
    }
  }

  if (s_need_full) {
    draw_screen();
  } else if (s_screen == SCR_HOME && now - s_last_status >= STATUS_REFRESH_MS) {
    s_last_status = now;
    refresh_home_status();
  }

  // Touch (ignore boot phantoms, then debounce)
  if ((int32_t)(now - s_ignore_touch_until) < 0) return;
  if (now - s_touch_guard < 220) return;
  TouchPt t = read_touch();
  if (t.pressed) {
    s_touch_guard = now;
    handle_touch(t.x, t.y);
    if (s_need_full && s_screen != SCR_ALERT) {
      draw_screen();
    }
  }
}

}  // namespace gfy
