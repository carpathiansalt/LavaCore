# TWANG firmware (PlatformIO)

ESP32 firmware for the TWANG connected LED game controller. Refactored from
the original Arduino `.ino` sketch into a modular PlatformIO project with:

- Input source abstraction (`MpuInput` fallback + `RemoteInput` over BLE)
- NimBLE GATT server implementing the protocol at `../docs/ble_protocol.md`
- A mode manager (v1 only ships `GAME_TWANG`; ambient/music slots reserved)
- NVS-backed settings with schema versioning
- FastLED on core 0, game loop on core 1

## Build

From this directory (`firmware/`):

```sh
pio run -e esp32dev
```

If `pio` is not on your PATH, use:

```sh
python -m platformio run -e esp32dev
```

## Flash the board (update firmware on the ESP32)

1. Connect the ESP32 with USB data cable (Windows: install CP210x/CH340 driver if the port does not appear).
2. From `firmware/`:

```sh
pio run -e esp32dev -t upload
```

If upload picks the wrong serial port:

```sh
pio device list
pio run -e esp32dev -t upload --upload-port COM5
```

Same with `python -m platformio` instead of `pio` if needed.

(Replace `COM5` with your port; on macOS/Linux use e.g. `/dev/cu.usbserial-*`.)

3. Optional - watch boot log (should report firmware version, e.g. **0.1.6**):

```sh
pio device monitor -b 115200
```

After flashing, the app **Settings -> Device -> Firmware** line should show the same version once connected over BLE.

### Multi-player / idle bring-up traces (optional)

In [`src/config/hw_config.h`](src/config/hw_config.h), set **one or both** to `1`, then rebuild and flash:

- **`TWANG_TRACE_GAME_INPUT`** - once per second: `stage`, `any_alive`, `all_game_over`, `active_player_count`, per-slot `g_remote_inputs[i].is_active()`, `slot_source(i)`, and `any_attack_tilt`. Also logs when `PLAY` hits `!any_alive` (transition to `GAMEOVER` or `DEAD`).
- **`TWANG_TRACE_CONTROL_SPACING`** - on each BLE control write: if inter-arrival for that slot **> 200 ms**, prints a line (compare to `kStaleMs` in `remote_input.cpp`).

Use **115200** baud. Set both back to **0** for production builds.

Debug build with serial joystick logs:

```
pio run -e esp32dev-debug -t upload
```

## Hardware (prototype)

- ESP32-WROOM-32 dev board
- WS2812B strip on `GPIO 16`, 5 V, common ground with controller
- Optional MPU6050 on I2C (SDA 21, SCL 22) for MPU fallback mode
- Optional speaker on DAC `GPIO 25`

Change pins and strip length via serial (`C=144`) or future BLE `config` char.

## Layout

```
src/
  main.cpp                  setup, RTOS task creation
  hal/                      hardware wrappers (LED output, storage)
  transport/                BLE peripheral, wire protocol structs
  input/                    IInputSource + MpuInput + RemoteInput + manager
  engine/                   ModeManager, frame scheduler
  apps/                     IApp + game_twang
  game/                     Enemy/Boss/Lava/Spawner/Conveyor/Particle/iSin
  config/                   Settings + defaults

tools/
  fake_client.py            bleak-based Python client for bring-up without the app
```

## Tests / bring-up

See `tools/fake_client.py` for a no-app sanity check:

```
python -m pip install bleak
python tools/fake_client.py
```

It scans for `TWANG-*`, connects, streams `control` packets at 40 Hz, and
prints incoming telemetry.
