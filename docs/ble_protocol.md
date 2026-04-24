# TWANG BLE Protocol v1.2

Authoritative specification of the BLE link between the TWANG ESP32 firmware
(peripheral) and the Flutter controller app (central). Both sides **must**
implement the byte layouts and rules below exactly. Mismatches are
not tolerated — the device rejects malformed packets.

## Multi-player model (v1.2+)

A single TWANG device hosts **up to 4 simultaneous players** on the same LED
strip. Players come from two kinds of input sources:

- The on-board MPU6050 joystick (optional, at most one per device).
- BLE-connected phone centrals (up to 4, connection-permitting).

Slots `0..3` are assigned by the device; centrals **must not** assume their
slot — they learn it from the `your_player_id` field in every telemetry
notification. Slot assignment rules:

1. If `input_mode = PHONE_ONLY` or the joystick is absent, the first phone to
   connect takes slot 0, the next slot 1, and so on.
2. If `input_mode = AUTO` (default) and the joystick is present, slot 0 is
   reserved for the joystick; phones fill slots 1..3 in connection order.
3. `input_mode = JOYSTICK_ONLY` gives slot 0 to the joystick; phones can
   still connect (for settings) but their control packets are ignored and
   they receive `your_player_id = 0xFF` (spectator).
4. When a central disconnects, its slot is freed and reused by the next new
   connection. Existing slots are never renumbered.

## Roles and connection

- **Peripheral**: ESP32 firmware (NimBLE).
- **Central**: Flutter app (`flutter_reactive_ble`).
- **Link-layer**: BLE 4.2+, 1M PHY. 2M PHY opportunistic if both sides support.
- **Security**: Just Works pairing + bonding is allowed but not required for v1.
- **Advertising name**: `TWANG-XXXX` where `XXXX` is the last 4 hex nibbles of
  the device MAC.
- **Connection interval**: negotiated on connect. Target 15 ms while a game is
  active. The peripheral requests a shorter interval via a Connection Parameter
  Update Request shortly after subscribe.
- **MTU**: the peripheral negotiates a larger ATT MTU (target **512**) so OTA
  data payloads can use multi-hundred-byte writes. Non-OTA characteristics
  still fit the default 23-byte ATT payload; the central should call GATT
  MTU exchange after connect when performing OTA. Effective application payload
  per write is approximately `negotiated_mtu - 3` (ATT header overhead).

## Service and characteristics

All UUIDs are 128-bit. The service UUID is custom; the first 16 bits are
unique to TWANG and the rest are fixed for this protocol family.

```
Service:      5d2f0001-9e8f-4b8c-9d8a-5a6b7c8d9e10  TWANG primary service
  info        5d2f0002-9e8f-4b8c-9d8a-5a6b7c8d9e10  read
  control     5d2f0003-9e8f-4b8c-9d8a-5a6b7c8d9e10  write-without-response
  telemetry   5d2f0004-9e8f-4b8c-9d8a-5a6b7c8d9e10  notify
  mode        5d2f0005-9e8f-4b8c-9d8a-5a6b7c8d9e10  read, write
  config      5d2f0006-9e8f-4b8c-9d8a-5a6b7c8d9e10  read, write (v1.1+)
  ota         5d2f0007-9e8f-4b8c-9d8a-5a6b7c8d9e10  read, write (OTA control, v2+)
  ambient     5d2f0008-9e8f-4b8c-9d8a-5a6b7c8d9e10  read, write (v1.2+)
  assignment  5d2f0009-9e8f-4b8c-9d8a-5a6b7c8d9e10  read           (v1.2+)
  ota_data    5d2f000a-9e8f-4b8c-9d8a-5a6b7c8d9e10  write-without-response (OTA image chunks)
```

All integers are **little-endian**. Fields are packed, no alignment padding.

### `info` (read, 16 bytes)

```
offset  size  field
0       1     u8   proto_version        // = 1
1       2     u16  fw_major
3       2     u16  fw_minor
5       2     u16  fw_patch
7       4     u32  board_id             // low 32 bits of BT MAC
11      4     u32  capabilities         // bitmap, see below
15      1     u8   data_pin             // LED data GPIO (e.g. 16, 2, 5) for OTA image selection
```

Capabilities bitmap (reserved bits must be zero):

```
bit 0  HAS_GAME              always 1 in v1
bit 1  HAS_MPU_FALLBACK      device has on-board MPU6050
bit 2  HAS_MIC               reserved, 0 in v1
bit 3  HAS_OTA               1 when BLE OTA is implemented (v2+)
bit 4  HAS_AMBIENT_MODES     1 in v1.2+
bit 5  HAS_MUSIC_MODES       reserved, 0 in v1
bit 6  HAS_NPLAYER           1 in v1.2+ (up to 4 concurrent players)
```

### `ota` (read/write, OTA control)

Little-endian packed structs. `version` byte is **1** for all v2 OTA control
packets.

**Read** returns a 12-byte **status** payload:

```
offset  size  field
0       1     u8   version              // = 1
1       1     u8   state                // 0 idle, 1 receiving, 2 error
2       1     u8   last_error           // see error codes below
3       1     u8   reserved
4       4     u32  bytes_received
8       4     u32  expected_size        // from last BEGIN, else 0
```

**Write** accepts either:

1. **BEGIN** — 12 bytes: `version=1`, `opcode=1`, `reserved u16=0`,
   `image_size u32`, `image_crc32 u32` (CRC-32 IEEE / Ethernet polynomial
   `0xEDB88320`, init `0xFFFFFFFF`, final XOR `0xFFFFFFFF`). The device
   erases the inactive OTA slot and opens an `esp_ota` session. **BEGIN is
   rejected while `mode_id == GAME_TWANG`** (switch to idle first).

2. **END** — 4 bytes: `version=1`, `opcode=2`, `reserved u16=0`. Validates
   received length and CRC, finalizes OTA, sets boot partition, and
   **restarts** the ESP32 on success.

3. **ABORT** — 4 bytes: `version=1`, `opcode=3`, `reserved u16=0`. Aborts
   the current session and returns to idle.

Error codes (`last_error`): `0` none, `1` busy (in game), `2` invalid, `3`
size, `4` flash, `5` CRC mismatch, `6` begin failed, `7` end/set_boot failed.

### `ota_data` (write-without-response, variable length)

Raw consecutive bytes of the firmware image. Only accepted after a successful
**BEGIN** and until **END** / **ABORT** / disconnect. Total length must match
`image_size` from **BEGIN**. Chunk size is limited by the negotiated ATT MTU
(minus overhead); centrals should stream sequentially.

### `control` (write-without-response, 16 bytes, 40 Hz target)

The central writes this every 25 ms while a game is active.

```
offset  size  field
0       1     u8   version              // = 1
1       1     u8   flags
                    bit0 attack_held
                    bit1 calibration_button
                    bits 2..7 reserved, must be 0
2       2     i16  tilt_centideg        // -9000..+9000, 0 = neutral
4       2     u16  attack_seq           // increments once per attack gesture
6       2     u16  seq                  // monotonic packet counter
8       1     u8   player_id            // 0 for v1
9       1     u8   group_id             // 0 for v1
10      2     i16  calibration_offset   // applied by central, mirrored for diag
12      2     u16  reserved             // must be 0
14      2     u16  reserved             // must be 0
```

**Semantics**

- `tilt_centideg` is the final game-ready tilt, post-calibration and filtering.
  Positive = right, negative = left. The device does **not** re-filter it.
- `attack_seq` is replicated state, not an event. The device fires one attack
  each time it observes `attack_seq` changed from the previous packet. This is
  idempotent under packet loss, reordering, and duplication. The central may
  increment `attack_seq` freely; wraparound at 65535 is fine.
- `seq` is a packet counter used by the device for telemetry RTT and loss
  statistics. It is not used for correctness.

### `telemetry` (notify, 32 bytes, 10 Hz target, v3+)

The peripheral notifies this periodically. Clients must subscribe (CCCD write)
before they will receive notifications. The payload is **broadcast** — all
subscribed centrals receive the same bytes. Per-connection state (which
player slot belongs to this central) is served separately via the
`assignment` characteristic (read-only, see below).

```
offset  size  field
0       1     u8   version              // = 3 (v1.2+ always sends v3)
1       1     u8   mode_id              // see mode enum
2       1     u8   player_count         // total active players (1..4)
3       1     u8   your_player_id       // broadcast: always 0xFF; real value
                                          // is read from `assignment` chr.
4       1     u8   global_flags
                    bit0 in_game
                    bit1 game_over
                    bit2 boss_alive
                    bit3 multiplayer_active  (player_count > 1)
                    bits 4..7 reserved
5       1     u8   reserved
6       2     u16  level
8       2     u16  fps_x10              // FPS * 10 (e.g. 600 = 60.0 fps)
10      2     u16  last_seq_ack         // latest seq observed across all centrals
12      2     u16  rtt_ms               // reserved in v1.2 (always 0)
14      2     u16  reserved

// Per-player block: 4 bytes × 4 slots = 16 bytes.
// Slots unused in the current session carry lives=0 / flags=0 / score=0.
16      1     u8   p0_lives
17      1     u8   p0_flags
                    bit0 alive           (cursor on strip)
                    bit1 attacking       (attack active this frame)
                    bit2 joystick        (1 = driven by MPU6050, 0 = phone)
                    bits 3..7 reserved
18      2     u16  p0_score_x10         // score / 10, saturates at 0xFFFF

20      4     bytes  p1_lives, p1_flags, p1_score_x10   // same layout as p0
24      4     bytes  p2_lives, p2_flags, p2_score_x10
28      4     bytes  p3_lives, p3_flags, p3_score_x10
```

`pN_score_x10` trades precision for space; authoritative stats are persisted
in NVS with full u32 resolution.

**Spectator mode.** When a central connects but cannot be assigned a slot
(e.g. in `JOYSTICK_ONLY` mode, or slots are full), the device still sends
telemetry with `your_player_id = 0xFF`. The app shows a read-only HUD and
silently drops any control writes it would have sent.

**Backwards compatibility.** v1.2 firmware no longer emits the v2 telemetry
layout — `version` is always 3. Older (v1/v2) apps will decode zeros for
bytes they do not recognise. The app UI must handle `version >= 3` as the
happy path; on older versions it can degrade to single-player HUD using
`p0_*` fields.

### `assignment` (read, 2 bytes, v1.2+)

Per-connection read that returns the player slot assigned to the requesting
central. Because NimBLE's `onRead` callback receives the connection descriptor,
each central gets its own value (this is the only characteristic that truly
varies per central).

```
offset  size  field
0       1     u8   your_player_id     // 0..TWANG_MAX_PLAYERS-1, or 0xFF (spectator)
1       1     u8   player_count       // current total active players
```

**Flow:**

1. On every reconnect, the central reads `assignment` once it has finished
   discovering services. The value lets the app render a "You are Player N"
   banner, pick a HUD color, etc.
2. If `your_player_id == 0xFF`, the central is a spectator. It should still
   subscribe to telemetry (for HUD display) but stop sending control writes.
3. The central may read `assignment` again at any time (e.g. when the user
   flips `input_mode`) to pick up a re-assignment without a full reconnect.

### `config` (read + write, 16 bytes, v1.1+)

Runtime-mutable, persisted device configuration. The central reads once on
connect to populate the settings UI, and writes whenever the user changes a
value. All fields are echoed unchanged if the central does not want to modify
them (the device does not diff — a write replaces the full config), so the
central must always read-modify-write.

```
offset  size  field
0       1     u8   version              // = 1
1       1     u8   input_mode           // see input_mode enum below
2       2     u16  led_count            // MIN_LEDS..MAX_LEDS (hw_config.h)
4       1     u8   led_brightness       // 10..255
5       1     u8   joystick_deadzone    // 3..12
6       2     u16  attack_threshold     // 20000..35000 (MPU6050 wobble units)
8       1     u8   lives_per_level      // 3..9
9       1     u8   audio_volume         // 0..255
10      2     u16  flags
                    bit0 persist_to_nvs (1 = save, 0 = apply only; defaults to 1)
                    bits 1..15 reserved, must be 0
12      4     u32  reserved             // must be 0
```

Input mode enum:

```
0  INPUT_AUTO            joystick (if present) takes slot 0; phones fill 1..3
1  INPUT_JOYSTICK_ONLY   only the MPU6050 plays; phones become spectators
2  INPUT_PHONE_ONLY      only phones play; MPU6050 is ignored
3  INPUT_MULTIPLAYER     DEPRECATED alias for INPUT_AUTO (still accepted for
                         pre-v1.2 apps; device silently treats it as AUTO)
```

**Write semantics**

- Writes out of range clamp to the valid range (device does not reject).
- `persist_to_nvs = 0` applies the config live but does not save it — useful
  for real-time previews (e.g. dragging a brightness slider).
- `persist_to_nvs = 1` saves to NVS; survives reboot.
- The device re-reads the characteristic value after applying the write, so
  the central can verify its change took effect by reading again.

**Mode switching behaviour**

- When `input_mode` changes, the device re-runs slot assignment at the end
  of the current level (or immediately if the game is idle). Existing
  cursors continue at their current positions until then.
- The central receives its (possibly new) `your_player_id` via the next
  telemetry notify — no explicit handshake is required.

### `ambient` (read + write, 16 bytes, v1.2+)

Runtime controls for the **ambient light** app (`MODE_AMBIENT`). The central
writes this characteristic to switch presets and tweak color/speed without
a full mode change. Reads return the currently applied state. Writes only
take effect while the device is in `MODE_AMBIENT`; in other modes the value
is remembered but the strip does not react until the mode is re-entered.

```
offset  size  field
0       1     u8   version              // = 1
1       1     u8   preset_id            // see preset enum below
2       1     u8   r                    // primary color red   (0..255)
3       1     u8   g                    // primary color green (0..255)
4       1     u8   b                    // primary color blue  (0..255)
5       1     u8   brightness           // overrides led_brightness while ambient
6       1     u8   speed                // 0 = static, 255 = fastest
7       1     u8   flags
                    bit0 enabled        (1 = render this preset, 0 = blackout)
                    bit1 persist        (1 = save to NVS, 0 = apply only)
                    bits 2..7 reserved
8       8     u64  reserved             // must be 0
```

Ambient preset enum:

```
0  SOLID           // flat color
1  BREATHING       // fade in/out of primary color
2  RAINBOW         // moving rainbow, speed controls cycle rate
3  FIRE            // Fire2012 animation (primary color hints tint)
4  CHASE           // single dot chasing along the strip in primary color
5  SPARKLE         // twinkling primary-color pixels
```

### `mode` (read / write, 2 bytes)

Selects which app the device is running.

```
offset  size  field
0       1     u8   mode_id
1       1     u8   submode_id           // reserved
```

Mode enum (v1.2):

```
0  MODE_IDLE
1  MODE_GAME_TWANG
2  MODE_AMBIENT              // active in v1.2+; see `ambient` characteristic
3  MODE_MUSIC                // reserved
```

Writing any unsupported mode returns ATT error `0x80` (application error).

## Behavioral rules (both sides)

1. **Watchdog (peripheral)**: if no `control` write is received for ~280 ms while
   a game is active, the device forces `tilt = 0` and masks all attacks until a
   new packet arrives. (Originally 200 ms; relaxed so multiple centrals at 40 Hz
   are less likely to hit the watchdog under normal scheduling.)
2. **Autofallback (peripheral)**: when no central is connected, the device uses
   its on-board MPU6050 (if `HAS_MPU_FALLBACK`) or freezes input.
3. **No reconnection state**: the central must not assume prior state after
   reconnect. It must re-read `info`, re-subscribe to `telemetry`, and resume
   streaming `control`. The peripheral resets `last_seq_ack = 0` on every
   connection.
4. **Calibration is central-side**: the central computes the offset and sends
   pre-calibrated `tilt_centideg`. The peripheral never calibrates on behalf of
   the central. `calibration_offset` is carried for diagnostics only.
5. **No secrets in packets**: all characteristics are readable by a bonded peer
   in v1. Nothing on this link is sensitive.

## Changelog

- **v1.0** (2025-10): Initial protocol (info, control, telemetry v1, mode).
  Config and OTA characteristics declared reserved.
- **v1.1** (2026-04): Config characteristic promoted to active; telemetry
  packet bumped to v2 with `p2_*` fields and multiplayer flags. `info` and
  `control` layouts unchanged. v1 clients remain forward-compatible on
  telemetry (they ignore the new bytes) and do not need to write `config`.
- **v1.2** (2026-04): True N-player support (up to 4 concurrent players).
  Telemetry bumped to v3 with `your_player_id`, `player_count`, and four
  per-player blocks; old v1/v2 layout is retired. `ambient` characteristic
  added for `MODE_AMBIENT`. `INPUT_MULTIPLAYER` input mode deprecated in
  favour of `INPUT_AUTO` (the device treats them identically).
  `CAP_HAS_AMBIENT_MODES` and `CAP_HAS_NPLAYER` capability bits light up.
- **v1.2.1** (2026-04): Control watchdog relaxed from 200 ms to ~280 ms on the
  reference firmware so multiple centrals at 40 Hz are less likely to see
  neutral-input glitches under normal BLE scheduling.

## Versioning

- `proto_version = 1` in `info`. If a central sees a higher value it must treat
  the device as incompatible and prompt the user to update the app.
- Adding fields requires bumping `version` within a packet and keeping the
  existing offsets stable. Old fields never move.
- New characteristics must use new UUIDs. Existing UUIDs are frozen.

## Reference hex fixtures (used by unit tests)

Control packet, tilt = -4500 centideg (-45 deg), attack held, attack_seq = 7, seq = 42:

```
01 01 6C EE 07 00 2A 00 00 00 00 00 00 00 00 00
```

Control packet, tilt = 0, idle, attack_seq = 0, seq = 0:

```
01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

Telemetry, level=3, score=1200, lives=2, mode=GAME_TWANG, in_game, fps=60.0, last_seq=42, rtt=28 ms:

```
01 01 02 01 03 00 B0 04 00 00 58 02 2A 00 1C 00 00 00 00 00
```
