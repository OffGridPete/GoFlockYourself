/**
 * GoFlockYourself — RGB LED + passive piezo
 */
#include "hardware.h"

namespace gfy {

// Soft PWM-ish via digital for RGB (active low)
static uint8_t s_r = 0, s_g = 0, s_b = 0;
static uint8_t s_mode = 0;  // 0=off 1=scan 2=idle 3=alert 4=error
static uint32_t s_anim_t = 0;
static bool s_anim_on = false;

// Non-blocking buzzer sequence
struct BuzzNote {
  uint16_t freq;
  uint16_t ms;
};
static const BuzzNote *s_seq = nullptr;
static uint8_t s_seq_len = 0;
static uint8_t s_seq_i = 0;
static uint32_t s_seq_t = 0;
static bool s_buzzing = false;

static void pin_led(uint8_t pin, bool on) {
#if LED_ACTIVE_LOW
  digitalWrite(pin, on ? LOW : HIGH);
#else
  digitalWrite(pin, on ? HIGH : LOW);
#endif
}

void hw_init() {
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  led_off();

  pinMode(BUZZER_PIN, OUTPUT);
#if BUZZER_ACTIVE_HIGH
  digitalWrite(BUZZER_PIN, LOW);
#else
  digitalWrite(BUZZER_PIN, HIGH);
#endif
}

void led_set(uint8_t r, uint8_t g, uint8_t b) {
  s_r = r;
  s_g = g;
  s_b = b;
  // Thresholded digital (no true PWM needed for status)
  pin_led(LED_R, r > 20);
  pin_led(LED_G, g > 20);
  pin_led(LED_B, b > 20);
}

void led_off() {
  s_mode = 0;
  pin_led(LED_R, false);
  pin_led(LED_G, false);
  pin_led(LED_B, false);
}

void led_status_scanning() { s_mode = 1; s_anim_t = 0; }
void led_status_idle()     { s_mode = 2; led_set(0, 40, 0); }
void led_status_alert()    { s_mode = 3; led_set(255, 0, 0); }
void led_status_error()    { s_mode = 4; led_set(180, 0, 180); }

void led_tick(uint32_t now) {
  if (s_mode == 1) {
    // Soft blue pulse ~400 ms
    if (now - s_anim_t >= 400) {
      s_anim_t = now;
      s_anim_on = !s_anim_on;
      if (s_anim_on) led_set(0, 30, 180);
      else           led_set(0, 0, 20);
    }
  } else if (s_mode == 3) {
    // Alert flash
    if (now - s_anim_t >= 120) {
      s_anim_t = now;
      s_anim_on = !s_anim_on;
      led_set(s_anim_on ? 255 : 40, 0, 0);
    }
  }
}

static void buzz_start_tone(uint16_t freq, uint16_t /*ms*/) {
  if (freq == 0) {
    noTone(BUZZER_PIN);
    s_buzzing = false;
  } else {
    tone(BUZZER_PIN, freq);
    s_buzzing = true;
  }
}

void buzzer_beep(uint16_t freq, uint16_t ms) {
  static BuzzNote one[1];
  one[0] = { freq, ms };
  s_seq = one;
  s_seq_len = 1;
  s_seq_i = 0;
  s_seq_t = millis();
  buzz_start_tone(freq, ms);
}

void buzzer_alert() {
  // Distinct 3-note "hit" pattern
  static const BuzzNote hit[] = {
    { 1800, 70 },
    { 0,    40 },
    { 2400, 90 },
    { 0,    40 },
    { 3000, 110 },
  };
  s_seq = hit;
  s_seq_len = sizeof(hit) / sizeof(hit[0]);
  s_seq_i = 0;
  s_seq_t = millis();
  buzz_start_tone(hit[0].freq, hit[0].ms);
}

void buzzer_boot() {
  static const BuzzNote boot[] = {
    { 880,  60 },
    { 0,    30 },
    { 1175, 60 },
    { 0,    30 },
    { 1568, 100 },
  };
  s_seq = boot;
  s_seq_len = sizeof(boot) / sizeof(boot[0]);
  s_seq_i = 0;
  s_seq_t = millis();
  buzz_start_tone(boot[0].freq, boot[0].ms);
}

void buzzer_tick(uint32_t now) {
  if (!s_seq || s_seq_i >= s_seq_len) {
    if (s_buzzing) {
      noTone(BUZZER_PIN);
      s_buzzing = false;
    }
    return;
  }
  if (now - s_seq_t >= s_seq[s_seq_i].ms) {
    s_seq_i++;
    s_seq_t = now;
    if (s_seq_i >= s_seq_len) {
      noTone(BUZZER_PIN);
      s_buzzing = false;
      s_seq = nullptr;
      return;
    }
    buzz_start_tone(s_seq[s_seq_i].freq, s_seq[s_seq_i].ms);
  }
}

}  // namespace gfy
