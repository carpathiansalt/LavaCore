#pragma once
#include <stdint.h>

// Frame-by-frame hooks that feed the telemetry loop. The mode manager keeps
// its own game-state telemetry; this module layers the BLE-level metrics
// (FPS, last_seq_ack, rtt_ms) on top and pumps notifications at 10 Hz.
namespace telemetry {

void begin();

// Call once per rendered frame. Increments the internal FPS counter.
void mark_frame();

// Runs the 10 Hz notifier. Safe to call from loop(); self-throttles.
void tick(uint32_t now_ms);

} // namespace telemetry
