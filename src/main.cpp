/**
 * GoFlockYourself
 * ===============
 * Passive Flock Safety ALPR detector for ESP32-2432S028R (Cheap Yellow Display).
 *
 * Primary method: WiFi promiscuous mode
 *   - Full OUI match on addr2 (TX) and addr1 (RX)
 *   - Wildcard probe request detection (empty SSID)
 *   - Channel hopping (1/6/11, full 1–13, or ascending)
 * Secondary: optional BLE (XUNTONG mfg, OUI, names, Raven UUIDs)
 *
 * Hardware: 2.8" ILI9341 + XPT2046 + RGB LED + optional piezo + microSD
 *
 * Research credits: @NitekryDPaul, DeFlockJoplin, colonelpanichacks/flock-you
 */
#include <Arduino.h>

#include "config.h"
#include "detection.h"
#include "radio.h"
#include "ui.h"
#include "logger.h"
#include "hardware.h"
#include "oui_list.h"
#include "types.h"

using namespace gfy;

static uint32_t s_last_hb = 0;

static void on_hit(const DetectEvent &ev, bool is_new_alert) {
  // Queue CSV only — never mount SD here (shared VSPI with touch; crashes CYD)
  logger_log_hit(ev);

  if (!is_new_alert) return;

  if (settings().audio_alert) {
    buzzer_alert();
  }
  if (settings().led_alert) {
    led_status_alert();
  }
  ui_show_alert(ev);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  GoFlockYourself  v" GFY_VERSION));
  Serial.println(F("  " GFY_HW_NAME));
  Serial.println(F("========================================"));

  hw_init();
  detection_init();
  ui_init();
  // Wire SD ↔ touch VSPI handoff (logger never calls SD in setup)
  logger_set_bus_hooks(ui_spi_release_for_sd, ui_spi_reclaim_after_sd);
  logger_init();
  radio_init();

  buzzer_boot();

  Serial.printf("[cfg] OUIs=%u  dwell=%ums  buzzer=GPIO%u  RGB=%u/%u/%u\n",
                (unsigned)FLOCK_OUI_COUNT,
                (unsigned)CHANNEL_DWELL_MS,
                (unsigned)BUZZER_PIN,
                (unsigned)LED_R, (unsigned)LED_G, (unsigned)LED_B);
  Serial.printf("[cfg] free heap=%u  flash=%u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFlashChipSize());
  Serial.println(F("[cfg] methods: OUI-TX, OUI-RX, wildcard probe, SSID keys"));
  Serial.println(F("[cfg] BLE off by default (enable in Methods menu)"));
  Serial.println(F("[cfg] receive-only — no TX, no association"));

  // Auto-start scanning
  radio_start();
  led_status_scanning();
  ui_force_redraw();

  Serial.println(F("[boot] scanning — drive carefully, watch the yellow box"));
}

void loop() {
  const uint32_t now = millis();

  // Radio hop / BLE slice
  radio_tick(now);

  // Drain detection queue → alerts / UI (SD only queued here)
  detection_drain(on_hit);

  // Non-blocking buzzer + LED animations
  buzzer_tick(now);
  led_tick(now);

  // UI + touch
  ui_tick(now);

  // Flush SD only when NOT on the detection alert screen — mounting SD
  // during alert redraw is what rebooted the board onto the menu.
  if (ui_screen() != SCR_ALERT) {
    logger_tick(now);
  }

  // Serial heartbeat
  if (now - s_last_hb >= HEARTBEAT_MS) {
    s_last_hb = now;
    Stats &st = stats();
    Serial.printf(
        "[hb] ch=%u any=%lu mgmt=%lu probe=%lu wild=%lu oui=%lu hits=%lu/%lu heap=%u scan=%d\n",
        (unsigned)st.current_channel,
        (unsigned long)st.rx_frames,
        (unsigned long)st.rx_mgmt,
        (unsigned long)st.rx_probe,
        (unsigned long)st.rx_wildcard,
        (unsigned long)st.oui_sightings,
        (unsigned long)st.hits_total,
        (unsigned long)st.hits_unique,
        (unsigned)ESP.getFreeHeap(),
        (int)radio_is_scanning());

    if (st.rx_frames == 0 && radio_is_scanning()) {
      Serial.println(F("[hb] no frames yet — check antenna / wait near WiFi"));
    } else if (st.rx_frames > 0 && st.hits_total == 0) {
      Serial.println(F("[hb] RF alive; no Flock signatures yet"));
    }

    if (ESP.getFreeHeap() < HEAP_WARN_BYTES) {
      Serial.printf("[warn] low heap %u\n", (unsigned)ESP.getFreeHeap());
    }
  }

  // Yield to WiFi task
  delay(2);
}
