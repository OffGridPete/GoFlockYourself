/**
 * GoFlockYourself — detection engine (queue, dedupe, history)
 */
#pragma once

#include "types.h"
#include "config.h"

namespace gfy {

void detection_init();
Settings &settings();
Stats &stats();

/** ISR-safe: post a hit from sniffer / BLE. Returns false if queue full. */
bool detection_enqueue(const DetectEvent &ev);

/** Drain queue; returns number processed. Calls on_hit for each alerted hit. */
typedef void (*HitCallback)(const DetectEvent &ev, bool is_new_alert);
uint8_t detection_drain(HitCallback on_hit);

const HistoryEntry *history_get(size_t index);  // 0 = most recent
size_t history_count();
void history_clear();

/** Last alerted detection (for status UI) */
bool last_hit(DetectEvent &out, uint32_t &millis_at);

}  // namespace gfy
