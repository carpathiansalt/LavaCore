#pragma once
#include <stdint.h>

#include "input/input_source.h"
#include "transport/protocol.h"

// One-per-phone input source driven by BLE control packets. A ring of these
// lives in input_manager (indexed by player slot). Thread-safe: the BLE task
// writes packets via submit(); the render task reads via update().
class RemoteInput final : public IInputSource {
  public:
    /// [slot] is the player index (0..TWANG_MAX_PLAYERS-1); used only for
    /// optional [TWANG_TRACE_CONTROL_SPACING] inter-arrival logs.
    void submit(uint8_t slot, const ControlPacket& pkt, uint32_t rx_millis);

    void update() override;

    int  tilt()   const override { return _tilt; }
    int  wobble() const override { return _wobble; }

    // Active = connected AND a fresh packet within kStaleMs. See update().
    bool is_active() const override { return _active; }

    bool connected() const { return _connected; }

    // Connection handle this slot is bound to (0xFFFF = no peer).
    uint16_t conn_handle() const { return _conn_handle; }

    // Telemetry helper — seq ack of the most recent ControlPacket.
    uint16_t last_seq_ack() const { return _last_seq_ack; }

    // Lifecycle. on_connect resets watchdog / attack state so each new peer
    // gets a clean session.
    void on_connect(uint16_t conn_handle);
    void on_disconnect();

  private:
    volatile ControlPacket _latest{};
    volatile uint32_t      _rx_millis       = 0;
    volatile uint16_t      _last_attack_seq = 0;
    volatile uint16_t      _last_seq_ack    = 0;
    volatile bool          _has_packet      = false;
    volatile bool          _connected       = false;
    volatile uint16_t      _conn_handle     = 0xFFFF;

    // Held timestamp of the last attack_seq transition. While within
    // kAttackHoldMs the source emits an above-threshold wobble pulse; this
    // latching protects against BLE jitter and render/BLE task race conditions.
    uint32_t _attack_fire_ms = 0;

    int  _tilt   = 0;
    int  _wobble = 0;
    bool _active = false;
};

// One RemoteInput per player slot. Unused slots stay in the disconnected state.
extern RemoteInput g_remote_inputs[TWANG_MAX_PLAYERS];

// Legacy alias. A lot of older code still references `g_remote_input`; we keep
// it pointing at slot 0 for source-level compatibility. New code should go
// through g_remote_inputs[] / input_manager.
extern RemoteInput& g_remote_input;
