#include "input/remote_input.h"

#include <Arduino.h>
#include <string.h>

#include "config/hw_config.h"
#include "config/settings.h"

// Packet staleness threshold. Per docs/ble_protocol.md the peripheral must
// force neutral input after ~200 ms of silence while a central is connected.
// Slightly relaxed so two centrals at 40 Hz do not starve each other on the
// same connection interval / ATT queue.
static constexpr uint32_t kStaleMs = 280;

RemoteInput g_remote_inputs[TWANG_MAX_PLAYERS];
RemoteInput& g_remote_input = g_remote_inputs[0];

void RemoteInput::submit(uint8_t slot, const ControlPacket& pkt, uint32_t rx_millis) {
#if TWANG_TRACE_CONTROL_SPACING
    static uint32_t s_prev_rx_ms[TWANG_MAX_PLAYERS];
    if (slot < TWANG_MAX_PLAYERS) {
        const uint32_t prev = s_prev_rx_ms[slot];
        if (prev != 0) {
            const uint32_t dt = rx_millis - prev;
            if (dt > 200) {
                Serial.printf(
                    "[twang.rx] slot%u control gap %u ms (watchdog stale=%ums) seq=%u\r\n",
                    static_cast<unsigned>(slot),
                    static_cast<unsigned>(dt),
                    static_cast<unsigned>(kStaleMs),
                    static_cast<unsigned>(pkt.seq));
            }
        }
        s_prev_rx_ms[slot] = rx_millis;
    }
#else
    (void)slot;
#endif
    // BLE task writer. Readers always snapshot into a local ControlPacket
    // inside update(), so a plain memcpy is fine.
    memcpy(const_cast<ControlPacket*>(&_latest), &pkt, sizeof(ControlPacket));
    _rx_millis    = rx_millis;
    _last_seq_ack = pkt.seq;
    _has_packet   = true;
}

void RemoteInput::on_connect(uint16_t conn_handle) {
    _connected       = true;
    _conn_handle     = conn_handle;
    _has_packet      = false;
    _last_attack_seq = 0;
    _last_seq_ack    = 0;
    _attack_fire_ms  = 0;
    _tilt            = 0;
    _wobble          = 0;
    _active          = false;
}

void RemoteInput::on_disconnect() {
    _connected      = false;
    _conn_handle    = 0xFFFF;
    _has_packet     = false;
    _active         = false;
    _attack_fire_ms = 0;
    _tilt           = 0;
    _wobble         = 0;
}

void RemoteInput::update() {
    if (!_connected || !_has_packet) {
        _active = false;
        _tilt = 0;
        _wobble = 0;
        return;
    }

    const uint32_t age = millis() - _rx_millis;
    if (age > kStaleMs) {
        _active = false;
        _tilt = 0;
        _wobble = 0;
        return;
    }
    _active = true;

    ControlPacket pkt;
    memcpy(&pkt, const_cast<const ControlPacket*>(&_latest), sizeof(pkt));

    int tilt_deg = pkt.tilt_centideg / 100;
    if (tilt_deg > 90)  tilt_deg = 90;
    if (tilt_deg < -90) tilt_deg = -90;

    if (abs(tilt_deg) < g_settings.joystick_deadzone) tilt_deg = 0;
    if (tilt_deg > 0) tilt_deg -= g_settings.joystick_deadzone;
    if (tilt_deg < 0) tilt_deg += g_settings.joystick_deadzone;
    _tilt = tilt_deg;

    // Attack is replicated state: observe attack_seq transitions and latch a
    // wobble pulse of (attack_threshold + 1) for kAttackHoldMs. Idempotent.
    static constexpr uint32_t kAttackHoldMs = 80;

    if (pkt.attack_seq != _last_attack_seq) {
        _last_attack_seq = pkt.attack_seq;
        _attack_fire_ms  = millis();
    }

    const uint32_t attack_age = millis() - _attack_fire_ms;
    if (_attack_fire_ms != 0 && attack_age <= kAttackHoldMs) {
        _wobble = g_settings.attack_threshold + 1;
    } else {
        _wobble = 0;
    }
}
