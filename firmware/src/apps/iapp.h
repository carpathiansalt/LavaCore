#pragma once
#include <stdint.h>

#include "transport/protocol.h"

// Interface implemented by every "app" the mode manager can run
// (TWANG game in v1; ambient and music modes in later firmware revisions).
//
// Contract:
//   - init() is called once when the app becomes active.
//   - tick() is called each render frame. The input manager has already
//     populated joystickTilt / joystickWobble for this frame.
//   - fill_telemetry() writes app-specific fields into the shared telemetry
//     struct; BLE-wide fields (last_seq_ack, rtt_ms) are filled by the
//     transport layer.
class IApp {
  public:
    virtual ~IApp() = default;

    virtual ModeId mode_id() const = 0;

    virtual void init() = 0;
    virtual void tick(uint32_t now_ms) = 0;

    // In-game state snapshot used to build telemetry.
    virtual void fill_telemetry(TelemetryPacket& out) const = 0;
};
