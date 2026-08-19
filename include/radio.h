/**
 * GoFlockYourself — WiFi promiscuous mode + optional BLE
 */
#pragma once

#include <Arduino.h>
#include "types.h"

namespace gfy {

void radio_init();
void radio_start();
void radio_stop();
bool radio_is_scanning();
void radio_tick(uint32_t now);   // channel hop + BLE time-share
void radio_ble_setting_changed(bool enabled);  // call when UI toggles BLE

uint8_t radio_current_channel();
void radio_set_channel_plan(uint8_t plan);

}  // namespace gfy
