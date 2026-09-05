/**
 * GoFlockYourself — Serial + optional microSD logging
 *
 * Hit path only enqueues a CSV line (never touches the SD hardware).
 * logger_tick() flushes the queue when the UI is not in an alert.
 */
#pragma once

#include "types.h"
#include <stdint.h>

namespace gfy {

void logger_init();

/**
 * Flush at most one pending SD write. Call from loop() when it is safe
 * (not during full-screen detection alert).
 */
void logger_tick(uint32_t now);

bool logger_sd_ok();
bool logger_sd_present();
const char *logger_sd_status();       // short token: ready / none / format / error / off / idle
const char *logger_sd_detail();       // human line for Alerts

/** Persist ON/OFF and re-probe on enable. Safe while scanning. */
void logger_set_enabled(bool on);

/** Manual probe (Alerts menu). Prefer not to call during alert UI. */
bool logger_sd_probe();

/**
 * Serial log + enqueue CSV line for later SD append.
 * Safe to call from the detection path — does not mount SD.
 */
void logger_log_hit(const DetectEvent &ev);

void logger_printf(const char *fmt, ...);

typedef void (*LoggerBusHook)();
void logger_set_bus_hooks(LoggerBusHook release_fn, LoggerBusHook reclaim_fn);

/** True if there are queued lines waiting for SD. */
bool logger_has_pending();

}  // namespace gfy
