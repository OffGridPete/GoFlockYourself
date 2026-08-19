/**
 * GoFlockYourself — TFT UI for 240×320 ILI9341 + XPT2046
 */
#pragma once

#include "types.h"
#include <Arduino.h>

namespace gfy {

enum UiScreen : uint8_t {
  SCR_HOME = 0,
  SCR_MENU,
  SCR_METHODS,
  SCR_ALERTS_CFG,
  SCR_HISTORY,
  SCR_ABOUT,
  SCR_ALERT,       // full-screen detection flash
};

void ui_init();
void ui_tick(uint32_t now);
void ui_show_alert(const DetectEvent &ev);
void ui_force_redraw();
UiScreen ui_screen();

/** SPI bus handoff with SD card (both use VSPI on the CYD). */
void ui_spi_release_for_sd();
void ui_spi_reclaim_after_sd();

}  // namespace gfy
