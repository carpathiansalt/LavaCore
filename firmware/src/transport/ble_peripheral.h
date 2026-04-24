#pragma once
#include <stdint.h>

#include "transport/protocol.h"

namespace ble_peripheral {

// Initializes NimBLE, registers the TWANG service + characteristics, sets
// the advertising name to "TWANG-XXXX" (last 4 nibbles of the BT MAC), and
// begins advertising.
void begin();

// Returns true while a central is subscribed to telemetry notifications.
bool is_connected();

// Pushes a telemetry packet out as a notification if a subscriber exists.
// Safe to call at any cadence; drops silently when not connected.
void send_telemetry(const TelemetryPacket& tel);

} // namespace ble_peripheral
