#pragma once
#include "apps/iapp.h"

// Port of the original TWANG32 gameplay behind the IApp interface.
// Behavior is intended to match the legacy sketch byte-for-byte under the
// MPU input source. Input comes exclusively from the input_manager globals
// joystickTilt / joystickWobble.
class GameTwang final : public IApp {
  public:
    ModeId mode_id() const override { return MODE_GAME_TWANG; }

    void init() override;
    void tick(uint32_t now_ms) override;
    void fill_telemetry(TelemetryPacket& out) const override;
};
