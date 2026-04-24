#pragma once
// Wire protocol for the TWANG BLE link. MUST match docs/ble_protocol.md
// and lib/core/ble/api/packet_codec.dart byte-for-byte. Little-endian,
// packed layouts, no padding.

#include <stdint.h>

#define TWANG_PROTO_VERSION 1

// -- Service + characteristic UUIDs (128-bit, NimBLE string form) ----------
#define TWANG_SVC_UUID          "5d2f0001-9e8f-4b8c-9d8a-5a6b7c8d9e10"
#define TWANG_CHR_INFO_UUID     "5d2f0002-9e8f-4b8c-9d8a-5a6b7c8d9e10"
#define TWANG_CHR_CONTROL_UUID  "5d2f0003-9e8f-4b8c-9d8a-5a6b7c8d9e10"
#define TWANG_CHR_TELEMETRY_UUID "5d2f0004-9e8f-4b8c-9d8a-5a6b7c8d9e10"
#define TWANG_CHR_MODE_UUID     "5d2f0005-9e8f-4b8c-9d8a-5a6b7c8d9e10"
#define TWANG_CHR_CONFIG_UUID   "5d2f0006-9e8f-4b8c-9d8a-5a6b7c8d9e10"
#define TWANG_CHR_OTA_UUID      "5d2f0007-9e8f-4b8c-9d8a-5a6b7c8d9e10"
#define TWANG_CHR_OTA_DATA_UUID "5d2f000a-9e8f-4b8c-9d8a-5a6b7c8d9e10"
#define TWANG_CHR_AMBIENT_UUID  "5d2f0008-9e8f-4b8c-9d8a-5a6b7c8d9e10"
// Per-connection read: returns the player slot assigned to THIS central. Every
// central sees its own value thanks to NimBLE's `onRead(chr, desc)` hook.
#define TWANG_CHR_ASSIGNMENT_UUID "5d2f0009-9e8f-4b8c-9d8a-5a6b7c8d9e10"

// -- Capabilities bitmap ---------------------------------------------------
enum TwangCapability : uint32_t {
    CAP_HAS_GAME          = 1u << 0,
    CAP_HAS_MPU_FALLBACK  = 1u << 1,
    CAP_HAS_MIC           = 1u << 2,
    CAP_HAS_OTA           = 1u << 3,
    CAP_HAS_AMBIENT_MODES = 1u << 4,
    CAP_HAS_MUSIC_MODES   = 1u << 5,
    CAP_HAS_NPLAYER       = 1u << 6,
};

// -- Control packet flags --------------------------------------------------
enum ControlFlag : uint8_t {
    CTL_ATTACK_HELD         = 1u << 0,
    CTL_CALIBRATION_BUTTON  = 1u << 1,
};

// -- Telemetry v3 flags (global_flags byte) --------------------------------
enum TelemetryFlag : uint8_t {
    TEL_IN_GAME             = 1u << 0,
    TEL_GAME_OVER           = 1u << 1,
    TEL_BOSS_ALIVE          = 1u << 2,
    TEL_MULTIPLAYER_ACTIVE  = 1u << 3,
};

// -- Telemetry v3 per-player flags (pN_flags byte) -------------------------
enum TelemetryPlayerFlag : uint8_t {
    TEL_P_ALIVE     = 1u << 0,  // cursor is on the strip
    TEL_P_ATTACKING = 1u << 1,  // attack active this frame
    TEL_P_JOYSTICK  = 1u << 2,  // slot is driven by the on-board MPU
};

// Sentinel player_id for spectators (central connected but not playing).
static constexpr uint8_t TWANG_PLAYER_SPECTATOR = 0xFF;

// Hard cap on concurrent players (one joystick + phones, or phones only).
// Matches kMaxPlayers in input_manager.h and CONFIG_BT_NIMBLE_MAX_CONNECTIONS
// budget in platformio.ini.
static constexpr uint8_t TWANG_MAX_PLAYERS = 4;

// -- Mode ids --------------------------------------------------------------
enum ModeId : uint8_t {
    MODE_IDLE       = 0,
    MODE_GAME_TWANG = 1,
    MODE_AMBIENT    = 2,
    MODE_MUSIC      = 3,
};

// All wire structs use GCC's packed attribute; `#pragma pack` is ignored by
// some versions of xtensa-gcc, and we need byte-exact layouts.

struct __attribute__((packed)) InfoPacket {
    uint8_t  proto_version;
    uint16_t fw_major;
    uint16_t fw_minor;
    uint16_t fw_patch;
    uint32_t board_id;
    uint32_t capabilities;
    uint8_t  data_pin;
};
static_assert(sizeof(InfoPacket) == 16, "InfoPacket must be 16 bytes");

struct __attribute__((packed)) ControlPacket {
    uint8_t  version;
    uint8_t  flags;
    int16_t  tilt_centideg;
    uint16_t attack_seq;
    uint16_t seq;
    uint8_t  player_id;
    uint8_t  group_id;
    int16_t  calibration_offset;
    uint16_t reserved0;
    uint16_t reserved1;
};
static_assert(sizeof(ControlPacket) == 16, "ControlPacket must be 16 bytes");

// -- Telemetry v3 (32 bytes, per-connection payload) -----------------------
// Each BLE central receives a slightly different copy: `your_player_id`,
// `last_seq_ack` and `rtt_ms` are per-central. The per-player block is the
// same across all centrals (shared game state).
struct __attribute__((packed)) TelemetryPacket {
    uint8_t  version;          // = 3
    uint8_t  mode_id;
    uint8_t  player_count;     // 0..TWANG_MAX_PLAYERS
    uint8_t  your_player_id;   // broadcast value: always 0xFF in v1.2. Each
                               // central learns its real slot from the
                               // `assignment` characteristic (per-conn read).
    uint8_t  global_flags;     // TelemetryFlag bits
    uint8_t  reserved0;
    uint16_t level;
    uint16_t fps_x10;
    uint16_t last_seq_ack;     // latest ack observed across all centrals
    uint16_t rtt_ms;           // reserved in v1.2 (always 0)
    uint16_t reserved1;
    // Per-player slots: 4 bytes * TWANG_MAX_PLAYERS.
    struct __attribute__((packed)) Slot {
        uint8_t  lives;
        uint8_t  flags;        // TelemetryPlayerFlag bits
        uint16_t score_x10;    // score / 10, saturating
    } players[TWANG_MAX_PLAYERS];
};
static_assert(sizeof(TelemetryPacket) == 32, "TelemetryPacket v3 must be 32 bytes");

#define TWANG_TEL_VERSION 3

struct __attribute__((packed)) ModePacket {
    uint8_t mode_id;
    uint8_t submode_id;
};
static_assert(sizeof(ModePacket) == 2, "ModePacket must be 2 bytes");

// -- Config flags ----------------------------------------------------------
enum ConfigFlag : uint16_t {
    CFG_PERSIST_TO_NVS = 1u << 0,   // default 1; clear to preview without saving
};

struct __attribute__((packed)) ConfigPacket {
    uint8_t  version;              // = 1
    uint8_t  input_mode;           // InputMode enum
    uint16_t led_count;
    uint8_t  led_brightness;
    uint8_t  joystick_deadzone;
    uint16_t attack_threshold;
    uint8_t  lives_per_level;
    uint8_t  audio_volume;
    uint16_t flags;                // ConfigFlag bitmap
    uint32_t reserved;             // must be 0
};
static_assert(sizeof(ConfigPacket) == 16, "ConfigPacket must be 16 bytes");

// -- Ambient characteristic (v1.2+) ----------------------------------------
enum AmbientPreset : uint8_t {
    AMBIENT_SOLID     = 0,
    AMBIENT_BREATHING = 1,
    AMBIENT_RAINBOW   = 2,
    AMBIENT_FIRE      = 3,
    AMBIENT_CHASE     = 4,
    AMBIENT_SPARKLE   = 5,
};
static constexpr uint8_t AMBIENT_PRESET_COUNT = 6;

enum AmbientFlag : uint8_t {
    AMB_ENABLED = 1u << 0,
    AMB_PERSIST = 1u << 1,
};

struct __attribute__((packed)) AmbientPacket {
    uint8_t  version;              // = 1
    uint8_t  preset_id;            // AmbientPreset
    uint8_t  r;
    uint8_t  g;
    uint8_t  b;
    uint8_t  brightness;           // overrides led_brightness while ambient
    uint8_t  speed;                // 0 = static, 255 = fastest
    uint8_t  flags;                // AmbientFlag bitmap
    uint64_t reserved;             // must be 0
};
static_assert(sizeof(AmbientPacket) == 16, "AmbientPacket must be 16 bytes");

// -- OTA control (characteristic 0x0007) / data (0x000A) -------------------
#define TWANG_OTA_CTRL_VERSION 1

enum OtaCtrlOpcode : uint8_t {
    OTA_OP_BEGIN = 1,
    OTA_OP_END   = 2,
    OTA_OP_ABORT = 3,
};

enum OtaStateWire : uint8_t {
    OTA_WIRE_IDLE       = 0,
    OTA_WIRE_RECEIVING  = 1,
    OTA_WIRE_ERROR      = 2,
};

enum OtaErrorWire : uint8_t {
    OTA_ERR_NONE        = 0,
    OTA_ERR_BUSY_GAME   = 1,
    OTA_ERR_INVALID     = 2,
    OTA_ERR_SIZE        = 3,
    OTA_ERR_FLASH       = 4,
    OTA_ERR_CRC         = 5,
    OTA_ERR_BEGIN       = 6,
    OTA_ERR_END         = 7,
};

struct __attribute__((packed)) OtaBeginPacket {
    uint8_t  version;
    uint8_t  opcode;
    uint16_t reserved;
    uint32_t image_size;
    uint32_t image_crc32;
};
static_assert(sizeof(OtaBeginPacket) == 12, "OtaBeginPacket must be 12 bytes");

struct __attribute__((packed)) OtaSimpleCtrlPacket {
    uint8_t  version;
    uint8_t  opcode;
    uint16_t reserved;
};
static_assert(sizeof(OtaSimpleCtrlPacket) == 4, "OtaSimpleCtrlPacket must be 4 bytes");

struct __attribute__((packed)) OtaStatusPacket {
    uint8_t  version;
    uint8_t  state;
    uint8_t  last_error;
    uint8_t  reserved;
    uint32_t bytes_received;
    uint32_t expected_size;
};
static_assert(sizeof(OtaStatusPacket) == 12, "OtaStatusPacket must be 12 bytes");
