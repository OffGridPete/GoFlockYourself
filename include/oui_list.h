/**
 * GoFlockYourself — Flock Safety / related OUI prefixes
 *
 * Research credits:
 *   @NitekryDPaul — primary 30-prefix WiFi OUI list + addr1 technique
 *   Michael / DeFlockJoplin — 82:6B:F2 + wildcard probe signature
 *   IEEE MA-L B4:1E:52 registered to Flock Safety (2024-05-09)
 *
 * Matching is exact 3-octet. Some entries have U/L bit set (e.g. 82:6B:F2).
 */
#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

struct OuiPrefix {
  uint8_t b0, b1, b2;
  uint8_t conf;  // 0=high, 1=medium, 2=lower
};

// Complete compiled list (~41 entries): high + lower confidence prefixes
static const OuiPrefix FLOCK_OUIS[] = {
  // === Highest confidence (direct Flock + core research) ===
  { 0xB4, 0x1E, 0x52, 0 },  // IEEE reg to Flock Safety
  { 0x70, 0xC9, 0x4E, 0 },
  { 0x3C, 0x91, 0x80, 0 },
  { 0xD8, 0xF3, 0xBC, 0 },
  { 0x80, 0x30, 0x49, 0 },
  { 0xB8, 0x35, 0x32, 0 },
  { 0x14, 0x5A, 0xFC, 0 },
  { 0x74, 0x4C, 0xA1, 0 },
  { 0x08, 0x3A, 0x88, 0 },
  { 0x9C, 0x2F, 0x9D, 0 },
  { 0xC0, 0x35, 0x32, 0 },
  { 0x94, 0x08, 0x53, 0 },
  { 0xE4, 0xAA, 0xEA, 0 },  // LiteOn — common Falcon/Sparrow
  { 0xF4, 0x6A, 0xDD, 0 },
  { 0xF8, 0xA2, 0xD6, 0 },
  { 0x24, 0xB2, 0xB9, 0 },
  { 0x00, 0xF4, 0x8D, 0 },
  { 0xD0, 0x39, 0x57, 0 },
  { 0xE8, 0xD0, 0xFC, 0 },
  { 0xE0, 0x4F, 0x43, 0 },
  { 0xB8, 0x1E, 0xA4, 0 },
  { 0x70, 0x08, 0x94, 0 },
  { 0x58, 0x8E, 0x81, 0 },
  { 0xEC, 0x1B, 0xBD, 0 },
  { 0x3C, 0x71, 0xBF, 0 },
  { 0x58, 0x00, 0xE3, 0 },
  { 0x90, 0x35, 0xEA, 0 },
  { 0x5C, 0x93, 0xA2, 0 },
  { 0x64, 0x6E, 0x69, 0 },
  { 0x48, 0x27, 0xEA, 0 },
  { 0xA4, 0xCF, 0x12, 0 },
  { 0x82, 0x6B, 0xF2, 0 },  // DeFlockJoplin field contribution

  // === Lower confidence / FS Ext Battery (Silicon Labs family) ===
  { 0x04, 0x0D, 0x84, 1 },
  { 0x1C, 0x34, 0xF1, 1 },
  { 0x38, 0x5B, 0x44, 1 },
  { 0x94, 0x34, 0x69, 1 },
  { 0xB4, 0xE3, 0xF9, 1 },
  { 0xCC, 0xCC, 0xCC, 2 },
  { 0xF0, 0x82, 0xC0, 1 },

  // === Additional LiteOn ===
  { 0xE0, 0x0A, 0xF6, 1 },

  // === Related surveillance (SoundThinking / ShotSpotter) ===
  { 0xD4, 0x11, 0xD6, 2 },
};

static const size_t FLOCK_OUI_COUNT = sizeof(FLOCK_OUIS) / sizeof(FLOCK_OUIS[0]);

// SSID / name keyword fragments (case-insensitive match)
static const char *const SSID_KEYWORDS[] = {
  "flock", "flck", "test_flck", "penguin", "pigvision", "fs_", "raven"
};
static const size_t SSID_KEYWORD_COUNT =
    sizeof(SSID_KEYWORDS) / sizeof(SSID_KEYWORDS[0]);

// BLE name fragments
static const char *const BLE_NAME_KEYWORDS[] = {
  "flock", "raven", "penguin", "pigvision", "fs_", "xuntong"
};
static const size_t BLE_NAME_KEYWORD_COUNT =
    sizeof(BLE_NAME_KEYWORDS) / sizeof(BLE_NAME_KEYWORDS[0]);

// Raven proprietary 16-bit service UUID bases (GainSec research)
static const uint16_t RAVEN_SERVICE_UUIDS[] = {
  0x3100, 0x3200, 0x3300, 0x3400, 0x3500
};
static const size_t RAVEN_SERVICE_UUID_COUNT =
    sizeof(RAVEN_SERVICE_UUIDS) / sizeof(RAVEN_SERVICE_UUIDS[0]);

/** Exact 3-byte OUI match. Returns conf level or -1 if no match. */
inline int oui_match(const uint8_t *mac) {
  if (!mac) return -1;
  for (size_t i = 0; i < FLOCK_OUI_COUNT; i++) {
    if (mac[0] == FLOCK_OUIS[i].b0 &&
        mac[1] == FLOCK_OUIS[i].b1 &&
        mac[2] == FLOCK_OUIS[i].b2) {
      return (int)FLOCK_OUIS[i].conf;
    }
  }
  return -1;
}

inline bool oui_matches(const uint8_t *mac) {
  return oui_match(mac) >= 0;
}
