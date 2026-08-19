/**
 * GoFlockYourself — RGB LED + buzzer helpers
 */
#pragma once

#include "config.h"
#include <Arduino.h>

namespace gfy {

void hw_init();
void led_set(uint8_t r, uint8_t g, uint8_t b);  // 0–255 logical brightness
void led_off();
void led_status_scanning();   // soft blue pulse state machine
void led_status_idle();       // dim green
void led_status_alert();      // solid red
void led_status_error();      // solid magenta
void led_tick(uint32_t now);  // call from loop for animations

void buzzer_beep(uint16_t freq = BEEP_FREQ_HZ, uint16_t ms = BEEP_MS);
void buzzer_alert();          // multi-tone hit pattern
void buzzer_boot();           // short startup chirp
void buzzer_tick(uint32_t now);

}  // namespace gfy
