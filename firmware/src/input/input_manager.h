#pragma once
#include <stdint.h>

#include "input/input_source.h"
#include "transport/protocol.h"

class MpuInput;
class RemoteInput;

namespace input_manager {

// Number of concurrent players supported. Matches TWANG_MAX_PLAYERS in
// transport/protocol.h and the BLE connection budget in platformio.ini.
constexpr uint8_t kMaxPlayers = TWANG_MAX_PLAYERS;

// Tells the game / telemetry layer which source is feeding a slot this frame.
enum class SlotSource : uint8_t {
    None     = 0,
    Joystick = 1,   // on-board MPU6050
    Phone    = 2,   // BLE central (one of g_remote_inputs[])
};

void begin();

// Pulls fresh samples from every active source and populates playerTilt[] /
// playerWobble[] / slot_source[] for this frame. Call once per render tick.
void tick();

// Source currently driving the *primary* slot (player 0). Used by telemetry
// + diagnostics UI on the strip and in serial logs.
enum class Source { None, Mpu, Remote };
Source current();

// Mode the manager latched at the start of the current tick. Cached so the
// game loop doesn't need to re-read g_settings.input_mode multiple times.
uint8_t active_mode();

// True if ANY phone is actively streaming fresh control packets.
bool phone_active();

// Returns the first player slot a phone drives under the current mode,
// or 0xFF if no phone slot is in use. Used by legacy v1/v2 telemetry
// diagnostics.
uint8_t phone_player_id();

// Per-slot source this frame (None / Joystick / Phone).
SlotSource slot_source(uint8_t slot);

// Total number of active players this frame (0..kMaxPlayers). An "active"
// player is any slot whose source is not None.
uint8_t active_player_count();

// Notifies input_manager that a phone has just connected/disconnected on a
// specific BLE connection handle. The manager forwards this to the
// appropriate RemoteInput instance so seq acks / attack pulses reset.
void on_phone_connect(uint16_t conn_handle, uint8_t slot);
void on_phone_disconnect(uint16_t conn_handle, uint8_t slot);

// Route an incoming ControlPacket from a specific BLE central to its
// RemoteInput slot. Called by the BLE control-write callback.
void on_phone_control(uint8_t slot, const ControlPacket& pkt, uint32_t rx_ms);

} // namespace input_manager

// Legacy single-player globals. The bulk of game_twang.cpp still reads
// these; they always mirror player 0's values.
extern int joystickTilt;
extern int joystickWobble;

// Per-player inputs. playerTilt[i] is in the legacy [-90..+90] range and
// playerWobble[i] is >= 0. Unused slots stay at 0.
extern int playerTilt  [input_manager::kMaxPlayers];
extern int playerWobble[input_manager::kMaxPlayers];
