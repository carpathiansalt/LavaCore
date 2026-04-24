#pragma once
#include <stdint.h>

#include "apps/iapp.h"
#include "transport/protocol.h"

// Ambient light show. Renders one of the AmbientPreset animations onto the
// strip when MODE_AMBIENT is active. Holds its own state (preset / color /
// speed / brightness), persisted to NVS under a dedicated key.
class AppAmbient final : public IApp {
  public:
    ModeId mode_id() const override { return MODE_AMBIENT; }

    void init() override;
    void tick(uint32_t now_ms) override;
    void fill_telemetry(TelemetryPacket& out) const override;
};

namespace app_ambient {

// Load persisted state (preset / color / ...) from NVS, falling back to
// sensible defaults. Called once at boot; safe to call again.
void begin();

// Returns the current ambient state packed into a wire-format AmbientPacket.
// Used by the BLE `ambient` characteristic read.
AmbientPacket snapshot();

// Apply a new ambient configuration. Validates and clamps fields; optionally
// persists to NVS based on AMB_PERSIST in `pkt.flags`. Safe to call from the
// BLE task.
void apply(const AmbientPacket& pkt);

} // namespace app_ambient
