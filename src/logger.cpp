/**
 * GoFlockYourself — Serial + microSD CSV logging (crash-safe)
 *
 * WHY DEFERRED WRITES:
 *   On the CYD, microSD and the XPT2046 touch controller share VSPI.
 *   Mounting the SD card inside the detection/alert path (buzzer + full-screen
 *   redraw + SPI reconfigure) hard-faults / reboots the board. The reboot
 *   often lands on the menu via phantom touch.
 *
 * DESIGN:
 *   logger_log_hit()  → Serial + push CSV line into a static ring buffer
 *   logger_tick()     → pop one line, mount SD, append, unmount, restore touch
 *   Existing log files are never truncated (open mode "a" only).
 */
#include "logger.h"
#include "config.h"
#include "detection.h"

#include <SD.h>
#include <SPI.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace gfy {

static const char *const SD_MODE_APPEND = "a";
static const char *const SD_MODE_READ   = "r";
static const uint32_t SD_SPI_HZ         = 1000000;  // 1 MHz — most reliable

// Ring buffer of pre-formatted CSV lines (no heap)
static const uint8_t LOG_Q_LEN  = 12;
static const uint8_t LOG_LINE_N = 160;
static char     s_q[LOG_Q_LEN][LOG_LINE_N];
static uint8_t  s_q_head  = 0;
static uint8_t  s_q_tail  = 0;
static uint8_t  s_q_count = 0;

static bool     s_sd_present = false;
static bool     s_sd_ok      = false;
static bool     s_probe_done = false;
static uint32_t s_writes     = 0;
static uint32_t s_write_fails = 0;
static uint32_t s_log_bytes  = 0;
static uint32_t s_backoff_until = 0;
static uint32_t s_last_flush_ms = 0;
static bool     s_flushing   = false;

static LoggerBusHook s_release = nullptr;
static LoggerBusHook s_reclaim = nullptr;

// Dedicated VSPI wrapper for SD only (not the global SPI object)
static SPIClass s_sd_spi(VSPI);

void logger_set_bus_hooks(LoggerBusHook release_fn, LoggerBusHook reclaim_fn) {
  s_release = release_fn;
  s_reclaim = reclaim_fn;
}

bool logger_has_pending() { return s_q_count > 0; }

static void q_push(const char *line) {
  if (s_q_count >= LOG_Q_LEN) {
    // Drop oldest to make room
    s_q_tail = (uint8_t)((s_q_tail + 1) % LOG_Q_LEN);
    s_q_count--;
    Serial.println(F("[log] queue full — dropped oldest line"));
  }
  strncpy(s_q[s_q_head], line, LOG_LINE_N - 1);
  s_q[s_q_head][LOG_LINE_N - 1] = 0;
  s_q_head = (uint8_t)((s_q_head + 1) % LOG_Q_LEN);
  s_q_count++;
}

static bool q_peek(char *out, size_t n) {
  if (s_q_count == 0 || !out || n < 2) return false;
  strncpy(out, s_q[s_q_tail], n - 1);
  out[n - 1] = 0;
  return true;
}

static void q_pop() {
  if (s_q_count == 0) return;
  s_q_tail = (uint8_t)((s_q_tail + 1) % LOG_Q_LEN);
  s_q_count--;
}

static void pins_idle() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
}

/**
 * Mount card, append exactly one line, unmount, restore touch.
 * Never truncates an existing file.
 */
static bool sd_append_one_line(const char *line) {
  if (!line || !line[0]) return false;

  pins_idle();
  if (s_release) s_release();
  delay(5);

  // Map VSPI to SD padout
  s_sd_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  delay(5);

  if (!SD.begin(SD_CS, s_sd_spi, SD_SPI_HZ)) {
    Serial.println(F("[log] SD.begin failed"));
    SD.end();
    digitalWrite(SD_CS, HIGH);
    if (s_reclaim) s_reclaim();
    return false;
  }

  if (SD.cardType() == CARD_NONE) {
    Serial.println(F("[log] CARD_NONE"));
    SD.end();
    digitalWrite(SD_CS, HIGH);
    if (s_reclaim) s_reclaim();
    return false;
  }

  s_sd_present = true;

  // If file missing or empty → write header via APPEND (not "w")
  bool need_header = true;
  if (SD.exists(SD_LOG_PATH)) {
    File rf = SD.open(SD_LOG_PATH, SD_MODE_READ);
    if (rf) {
      if (rf.size() > 0) need_header = false;
      s_log_bytes = (uint32_t)rf.size();
      rf.close();
    }
  }

  File f = SD.open(SD_LOG_PATH, SD_MODE_APPEND);
  if (!f) {
    Serial.println(F("[log] open(a) failed"));
    SD.end();
    digitalWrite(SD_CS, HIGH);
    if (s_reclaim) s_reclaim();
    return false;
  }

  if (need_header || f.size() == 0) {
    f.println(
        F("millis,uptime_s,mac,oui,method,protocol,rssi,channel,confidence,label"));
  }

  const uint32_t before = (uint32_t)f.size();
  const size_t n = f.print(line);
  // Ensure newline
  if (line[strlen(line) - 1] != '\n') {
    f.print('\n');
  }
  f.flush();
  const uint32_t after = (uint32_t)f.size();
  f.close();

  SD.end();
  digitalWrite(SD_CS, HIGH);
  delay(5);
  if (s_reclaim) s_reclaim();

  if (n == 0 || after <= before) {
    Serial.printf("[log] write verify fail n=%u %lu→%lu\n",
                  (unsigned)n, (unsigned long)before, (unsigned long)after);
    return false;
  }

  s_log_bytes = after;
  s_sd_ok = true;
  s_probe_done = true;
  s_writes++;
  Serial.printf("[log] APPEND ok #%lu +%uB total=%lu qleft=%u\n",
                (unsigned long)s_writes,
                (unsigned)(after - before),
                (unsigned long)after,
                (unsigned)s_q_count);
  return true;
}

bool logger_sd_probe() {
#if !ENABLE_SD_LOGGING
  s_probe_done = true;
  return false;
#else
  // Probe = try to append nothing critical; just check mount
  pins_idle();
  if (s_release) s_release();
  delay(5);
  s_sd_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  delay(5);
  bool ok = SD.begin(SD_CS, s_sd_spi, SD_SPI_HZ);
  if (ok && SD.cardType() != CARD_NONE) {
    s_sd_present = true;
    s_sd_ok = true;
    if (SD.exists(SD_LOG_PATH)) {
      File rf = SD.open(SD_LOG_PATH, SD_MODE_READ);
      if (rf) {
        s_log_bytes = (uint32_t)rf.size();
        rf.close();
      }
    }
    Serial.printf("[log] probe OK existing_bytes=%lu\n",
                  (unsigned long)s_log_bytes);
  } else {
    s_sd_present = false;
    s_sd_ok = false;
    Serial.println(F("[log] probe: no card"));
  }
  SD.end();
  digitalWrite(SD_CS, HIGH);
  delay(5);
  if (s_reclaim) s_reclaim();
  s_probe_done = true;
  return s_sd_ok;
#endif
}

void logger_init() {
  Serial.println(F("[log] Serial ready @ 115200"));
  memset(s_q, 0, sizeof(s_q));
  s_q_head = s_q_tail = s_q_count = 0;
#if ENABLE_SD_LOGGING
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  settings().sd_logging = true;
  s_probe_done = false;
  s_sd_ok = false;
  Serial.println(F("[log] SD writes deferred (queue) — safe with touch UI"));
  Serial.println(F("[log] append-only — existing gfy_log.csv never wiped"));
#else
  settings().sd_logging = false;
  s_probe_done = true;
#endif
}

void logger_tick(uint32_t now) {
#if !ENABLE_SD_LOGGING
  (void)now;
  return;
#else
  if (s_flushing) return;
  if (s_q_count == 0) return;
  if (!settings().sd_logging) {
    // Drop queue if user disabled logging
    s_q_head = s_q_tail = s_q_count = 0;
    return;
  }
  if ((int32_t)(now - s_backoff_until) < 0) return;
  // Pace flushes so we never stall the UI for long bursts
  if (now - s_last_flush_ms < 300) return;

  s_flushing = true;
  s_last_flush_ms = now;

  char line[LOG_LINE_N];
  if (!q_peek(line, sizeof(line))) {
    s_flushing = false;
    return;
  }

  Serial.println(F("[log] flushing 1 queued line to SD..."));
  if (sd_append_one_line(line)) {
    q_pop();
    s_backoff_until = 0;
  } else {
    s_write_fails++;
    s_sd_ok = false;
    s_probe_done = true;
    // Back off so a bad card cannot freeze every loop
    s_backoff_until = now + 8000;
    Serial.println(F("[log] flush failed — backoff 8s (line kept in queue)"));
  }

  s_flushing = false;
#endif
}

bool logger_sd_ok() { return s_sd_ok; }
bool logger_sd_present() { return s_sd_present; }

const char *logger_sd_status() {
#if !ENABLE_SD_LOGGING
  return "disabled";
#else
  if (s_q_count > 0 && !s_sd_ok) return "queue";
  if (!s_probe_done && s_q_count == 0) return "idle";
  if (s_sd_ok) return "ready";
  if (s_sd_present) return "error";
  if (s_q_count > 0) return "queue";
  return "none";
#endif
}

void logger_printf(const char *fmt, ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
}

void logger_log_hit(const DetectEvent &ev) {
  char macs[18];
  char ouis[9];
  mac_to_str(ev.mac, macs, sizeof(macs));
  oui_to_str(ev.mac, ouis, sizeof(ouis));

  Serial.printf("[HIT] %s %s ch=%u rssi=%d conf=%u %s %s\n",
                method_name(ev.method),
                macs,
                (unsigned)ev.channel,
                (int)ev.rssi,
                (unsigned)ev.confidence,
                ev.protocol == PROTO_BLE ? "BLE" : "WiFi",
                ev.label[0] ? ev.label : "");

#if !ENABLE_SD_LOGGING
  return;
#else
  if (!settings().sd_logging) return;

  // Sanitize label
  char lab[24];
  lab[0] = 0;
  if (ev.label[0]) {
    size_t j = 0;
    for (size_t i = 0; ev.label[i] && j + 1 < sizeof(lab); i++) {
      char c = ev.label[i];
      if (c == ',' || c == '\n' || c == '\r') c = ' ';
      lab[j++] = c;
    }
    lab[j] = 0;
  }

  // Build CSV line only — NO SD hardware access here
  char line[LOG_LINE_N];
  snprintf(line, sizeof(line),
           "%lu,%lu,%s,%s,%s,%s,%d,%u,%u,%s\n",
           (unsigned long)millis(),
           (unsigned long)(millis() / 1000UL),
           macs, ouis,
           method_name(ev.method),
           ev.protocol == PROTO_BLE ? "ble" : "wifi",
           (int)ev.rssi,
           (unsigned)ev.channel,
           (unsigned)ev.confidence,
           lab);

  q_push(line);
  Serial.printf("[log] queued for SD (depth=%u)\n", (unsigned)s_q_count);
#endif
}

}  // namespace gfy
