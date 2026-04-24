# LavaCore

Open-source **firmware** and **documentation** for LavaCore / TWANG-style LED strip controllers (ESP32, BLE, WS2812).

The **Flutter app** (companion controller) is developed separately and is **not included** in this repository when published publicly. If you have the full workspace locally, see `PRIVATE_FLUTTER_APP.md`.

## Contents

| Path | Description |
|------|-------------|
| `firmware/` | PlatformIO ESP32 firmware (game, BLE GATT, OTA, ambient modes) |
| `docs/` | BLE protocol specification (`ble_protocol.md`) |
| `landing/` | Marketing / static site sources (optional) |
| `firmware/scripts/` | Helper scripts (e.g. OTA upload to Firebase Storage) |

## Firmware quick start

```bash
cd firmware
python -m platformio run -e esp32dev
python -m platformio run -e esp32dev -t upload
python -m platformio device monitor -b 115200
```

Build targets (see `firmware/platformio.ini`):

- `esp32dev` — GPIO 16 (TWANG32-style)
- `esp32-gpio2` — GPIO 2 (common WLED-style boards)
- `esp32-gpio5` — GPIO 5

## OTA images

Production `.bin` files for over-the-air updates are distributed via **Firebase Storage** (see app configuration). To upload freshly built images after authentication:

```powershell
cd firmware\scripts
.\upload_ota_firebase.ps1
```

## BLE protocol

Authoritative wire format: [`docs/ble_protocol.md`](docs/ble_protocol.md).

## License

Firmware and docs in this tree are intended to be shared openly; see `LICENSE` if present, or add one for your preferred terms.
