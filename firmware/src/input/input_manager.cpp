#include "input/input_manager.h"

#include <Arduino.h>

#include "config/settings.h"
#include "input/mpu_input.h"
#include "input/remote_input.h"

// Legacy single-player globals (mirror of slot 0).
int joystickTilt   = 0;
int joystickWobble = 0;

// Per-player globals used by the N-player game loop. Unused slots stay 0.
int playerTilt  [input_manager::kMaxPlayers] = {0};
int playerWobble[input_manager::kMaxPlayers] = {0};

namespace {

MpuInput                     s_mpu;
input_manager::Source        s_current        = input_manager::Source::None;
uint8_t                      s_active_mode    = INPUT_AUTO;
bool                         s_phone_active   = false;
input_manager::SlotSource    s_slot_source[input_manager::kMaxPlayers] = {};

inline uint8_t normalise_mode(uint8_t raw) {
    // Pre-v1.2 clients may still write INPUT_MULTIPLAYER; the device treats
    // it identically to AUTO now that true N-player routing is in place.
    return (raw == INPUT_MULTIPLAYER) ? INPUT_AUTO : raw;
}

void drive(uint8_t slot, IInputSource* src, input_manager::SlotSource kind) {
    if (slot >= input_manager::kMaxPlayers) return;
    if (src != nullptr && src->is_active()) {
        playerTilt  [slot] = src->tilt();
        playerWobble[slot] = src->wobble();
        s_slot_source[slot] = kind;
    } else {
        playerTilt  [slot] = 0;
        playerWobble[slot] = 0;
        s_slot_source[slot] = input_manager::SlotSource::None;
    }
}

} // namespace

namespace input_manager {

void begin() {
    s_mpu.begin();
}

void tick() {
    // Pump every source so their staleness watchdogs keep ticking.
    for (auto& ri : g_remote_inputs) ri.update();
    s_mpu.update();

    s_active_mode = normalise_mode(g_settings.input_mode);

    s_phone_active = false;
    for (auto& ri : g_remote_inputs) {
        if (ri.is_active()) { s_phone_active = true; break; }
    }

    for (uint8_t i = 0; i < kMaxPlayers; ++i) {
        drive(i, nullptr, SlotSource::None);
    }

    const bool joystick_ok = s_mpu.is_active();

    // Slot 0: joystick takes it in AUTO (if present) or JOYSTICK_ONLY. In
    // PHONE_ONLY the on-board joystick is ignored even if present.
    if (s_active_mode == INPUT_JOYSTICK_ONLY ||
        (s_active_mode == INPUT_AUTO && joystick_ok &&
         !g_remote_inputs[0].is_active())) {
        drive(0, joystick_ok ? static_cast<IInputSource*>(&s_mpu) : nullptr,
              SlotSource::Joystick);
    }

    // Every other slot is driven directly by its bound RemoteInput. The BLE
    // peripheral already placed each connecting phone into the right slot
    // based on mode, so we just pump them here.
    if (s_active_mode != INPUT_JOYSTICK_ONLY) {
        for (uint8_t i = 0; i < kMaxPlayers; ++i) {
            // Don't stomp slot 0 if the joystick just claimed it.
            if (i == 0 && s_slot_source[0] == SlotSource::Joystick) continue;
            if (g_remote_inputs[i].is_active()) {
                drive(i, &g_remote_inputs[i], SlotSource::Phone);
            }
        }
    }

    joystickTilt   = playerTilt  [0];
    joystickWobble = playerWobble[0];

    switch (s_slot_source[0]) {
        case SlotSource::Joystick: s_current = Source::Mpu;    break;
        case SlotSource::Phone:    s_current = Source::Remote; break;
        default:                   s_current = Source::None;
    }
}

Source  current()      { return s_current; }
uint8_t active_mode()  { return s_active_mode; }
bool    phone_active() { return s_phone_active; }

uint8_t phone_player_id() {
    for (uint8_t i = 0; i < kMaxPlayers; ++i) {
        if (s_slot_source[i] == SlotSource::Phone) return i;
    }
    return 0xFF;
}

SlotSource slot_source(uint8_t slot) {
    if (slot >= kMaxPlayers) return SlotSource::None;
    return s_slot_source[slot];
}

uint8_t active_player_count() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kMaxPlayers; ++i) {
        if (s_slot_source[i] != SlotSource::None) ++n;
    }
    return n;
}

void on_phone_connect(uint16_t conn_handle, uint8_t slot) {
    if (slot >= kMaxPlayers) return;
    g_remote_inputs[slot].on_connect(conn_handle);
}

void on_phone_disconnect(uint16_t /*conn_handle*/, uint8_t slot) {
    if (slot >= kMaxPlayers) return;
    g_remote_inputs[slot].on_disconnect();
}

void on_phone_control(uint8_t slot, const ControlPacket& pkt, uint32_t rx_ms) {
    if (slot >= kMaxPlayers) return;
    g_remote_inputs[slot].submit(slot, pkt, rx_ms);
}

} // namespace input_manager

bool mpu_input_probed_present() { return s_mpu.probed_present(); }
