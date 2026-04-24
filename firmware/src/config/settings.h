#pragma once

#include <Arduino.h>
#include <stdint.h>

// Runtime-mutable, persisted settings. Written to NVS through hal/storage.
// Schema-versioned so future firmware upgrades can migrate cleanly.

#define SETTINGS_SCHEMA_VERSION  5   // bump when the struct layout changes
                                     // v4: default led_count 70 -> 62 (trim)
                                     // v5: add input_mode + player-related fields

// Ranges (kept compatible with the original TWANG32 project)
#define MIN_LEDS                 60
// Absolute ceiling; actual cap depends on strip type (see hw_config.h MAX_LEDS)
#define ABS_MAX_LEDS             1000

#define DEFAULT_BRIGHTNESS       150
#define MIN_BRIGHTNESS           10
#define MAX_BRIGHTNESS           255

#define MAX_PLAYER_SPEED         10
#define LIVES_PER_LEVEL          3
#define MIN_LIVES_PER_LEVEL      3
#define MAX_LIVES_PER_LEVEL      9

#define DEFAULT_ATTACK_THRESHOLD 30000
#define MIN_ATTACK_THRESHOLD     20000
#define MAX_ATTACK_THRESHOLD     35000

#define DEFAULT_JOYSTICK_DEADZONE 8
#define MIN_JOYSTICK_DEADZONE     3
#define MAX_JOYSTICK_DEADZONE     12

#define DEFAULT_VOLUME           20
#define MIN_VOLUME               0
#define MAX_VOLUME               255

// Default strip length for first boot. Matches the current 62 LED prototype
// (WS2812B 1 m strip trimmed to remove a damaged tail segment).
#define DEFAULT_LED_COUNT        62

// -- Input mode -----------------------------------------------------------
// Mirrors the `input_mode` byte in the BLE config characteristic. Determines
// whether the device listens to the MPU6050, the phone, both (multiplayer),
// or auto-selects based on connection state.
enum InputMode : uint8_t {
    INPUT_AUTO          = 0,  // phone wins if connected, else MPU6050
    INPUT_JOYSTICK_ONLY = 1,  // MPU6050 only, phone writes ignored
    INPUT_PHONE_ONLY    = 2,  // phone only, MPU6050 never polled
    INPUT_MULTIPLAYER   = 3,  // MPU6050 drives P0, phone drives P1
};

#define DEFAULT_INPUT_MODE       INPUT_AUTO

struct Settings {
    uint8_t  schema_version;

    uint16_t led_count;
    uint8_t  led_pin;           // GPIO for strip data line
    uint8_t  led_brightness;

    uint8_t  joystick_deadzone;
    uint16_t attack_threshold;

    uint8_t  audio_volume;
    uint8_t  lives_per_level;

    // v5+
    uint8_t  input_mode;        // see InputMode enum

    // Lifetime stats
    uint16_t games_played;
    uint32_t total_points;
    uint16_t high_score;
    uint16_t boss_kills;
};

extern Settings g_settings;

// Keeps the original global timer used by the screensaver logic.
extern long g_last_input_time_ms;

namespace twang_settings {

void load();          // read from NVS, or initialize defaults if missing/mismatch
void save();
void reset_defaults();
void dump_serial();   // print current values to Serial

// Back-compat with the legacy serial menu.
void handle_serial_char(char in_char);
void change(char param_code, uint16_t new_value);

} // namespace twang_settings
