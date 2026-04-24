// TWANG firmware entry point.
// Spawned tasks:
//   loop() on Arduino core (core 1): renders frames, runs the game + telemetry pump.
//   fast_led_show_task (core 0, created by led_output::begin): drives FastLED.
//   NimBLE runs its own internal host task (core 0).

#include <Arduino.h>

#include "config/hw_config.h"
#include "config/settings.h"
#include "engine/mode_manager.h"
#include "engine/telemetry.h"
#include "hal/led_output.h"
#include "input/input_manager.h"
#include "transport/ble_peripheral.h"

namespace {
#if defined(LED_BUILTIN)
constexpr uint8_t kStatusLedPin = LED_BUILTIN;
#else
constexpr uint8_t kStatusLedPin = 2; // Common onboard LED pin on ESP32 dev boards.
#endif

void status_led_begin() {
    pinMode(kStatusLedPin, OUTPUT);
    digitalWrite(kStatusLedPin, LOW);
}

void status_led_tick(uint32_t now_ms) {
    // Visual status:
    // - BLE connected: solid ON.
    // - BLE advertising / idle: 1 Hz heartbeat blink.
    if (ble_peripheral::is_connected()) {
        digitalWrite(kStatusLedPin, HIGH);
        return;
    }
    const bool blink_on = ((now_ms / 500) % 2) == 0;
    digitalWrite(kStatusLedPin, blink_on ? HIGH : LOW);
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.printf("\r\nTWANG firmware v%d.%d.%d booting\r\n",
                  TWANG_FW_MAJOR, TWANG_FW_MINOR, TWANG_FW_PATCH);

    twang_settings::load();
    twang_settings::dump_serial();

    status_led_begin();
    led_output::begin();
    input_manager::begin();
    mode_manager::begin();
    ble_peripheral::begin();
    telemetry::begin();

    Serial.println("[setup] done");
}

void loop() {
    while (Serial.available()) twang_settings::handle_serial_char(Serial.read());

    const uint32_t now = millis();
    status_led_tick(now);
    mode_manager::tick(now);
    telemetry::mark_frame();
    telemetry::tick(now);
}
