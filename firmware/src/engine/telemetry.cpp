#include "engine/telemetry.h"

#include <Arduino.h>
#include <string.h>

#include "config/settings.h"
#include "engine/mode_manager.h"
#include "input/input_manager.h"
#include "input/remote_input.h"
#include "transport/ble_peripheral.h"
#include "transport/protocol.h"

namespace {

constexpr uint32_t kNotifyIntervalMs = 100;   // 10 Hz
constexpr uint32_t kFpsWindowMs      = 1000;

uint32_t s_last_notify_ms = 0;

uint32_t s_fps_window_start_ms = 0;
uint32_t s_fps_counter         = 0;
uint16_t s_fps_x10_latched     = 0;

} // namespace

namespace telemetry {

void begin() {
    s_last_notify_ms      = millis();
    s_fps_window_start_ms = s_last_notify_ms;
    s_fps_counter         = 0;
    s_fps_x10_latched     = 0;
}

void mark_frame() {
    s_fps_counter++;
    const uint32_t now = millis();
    const uint32_t dt  = now - s_fps_window_start_ms;
    if (dt >= kFpsWindowMs) {
        uint32_t fps_x10 = (s_fps_counter * 10000ul) / dt;
        if (fps_x10 > 0xFFFFu) fps_x10 = 0xFFFFu;
        s_fps_x10_latched     = static_cast<uint16_t>(fps_x10);
        s_fps_counter         = 0;
        s_fps_window_start_ms = now;
    }
}

void tick(uint32_t now_ms) {
    if (!ble_peripheral::is_connected()) {
        s_last_notify_ms = now_ms;
        return;
    }
    if (now_ms - s_last_notify_ms < kNotifyIntervalMs) return;
    s_last_notify_ms = now_ms;

    TelemetryPacket tel{};
    tel.version        = TWANG_TEL_VERSION;          // v3
    tel.your_player_id = TWANG_PLAYER_SPECTATOR;     // broadcast; per-conn
                                                     // value served by the
                                                     // `assignment` chr.

    // Start with the current app's state (fills mode_id, global_flags, level,
    // per-player slots).
    mode_manager::fill_telemetry(tel);

    tel.fps_x10 = s_fps_x10_latched;

    // Aggregate last_seq_ack across all RemoteInput instances: use the
    // maximum seq observed. This is purely a "device is hearing you" hint.
    uint16_t seq_ack = 0;
    for (uint8_t i = 0; i < TWANG_MAX_PLAYERS; ++i) {
        if (g_remote_inputs[i].last_seq_ack() > seq_ack) {
            seq_ack = g_remote_inputs[i].last_seq_ack();
        }
    }
    tel.last_seq_ack = seq_ack;
    tel.rtt_ms       = 0;

    ble_peripheral::send_telemetry(tel);
}

} // namespace telemetry
