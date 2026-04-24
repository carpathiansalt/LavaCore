#pragma once
#include <stdint.h>

#include "apps/iapp.h"
#include "transport/protocol.h"

// Picks the active app. v1 hard-selects GAME_TWANG but the API leaves room
// for ambient and music modes without protocol churn.
namespace mode_manager {

void begin();
void tick(uint32_t now_ms);

// Returns the current mode id.
ModeId current_mode();

// Requested via BLE `mode` characteristic. Returns false if unsupported.
bool request_mode(ModeId mode, uint8_t submode);

// Snapshot the currently running app's state into the telemetry packet.
void fill_telemetry(TelemetryPacket& out);

} // namespace mode_manager
