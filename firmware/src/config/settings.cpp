#include "config/settings.h"

#include <Arduino.h>
#include <FastLED.h>

#include "config/hw_config.h"
#include "hal/storage.h"

namespace {

constexpr int kReadBufferLen = 10;
char     s_read_buffer[kReadBufferLen];
uint8_t  s_read_index = 0;

} // namespace

namespace twang_settings {

void reset_defaults() {
    g_settings.schema_version   = SETTINGS_SCHEMA_VERSION;
    g_settings.led_count        = DEFAULT_LED_COUNT;
    g_settings.led_pin          = TWANG_DATA_PIN;
    g_settings.led_brightness   = DEFAULT_BRIGHTNESS;
    g_settings.joystick_deadzone = DEFAULT_JOYSTICK_DEADZONE;
    g_settings.attack_threshold = DEFAULT_ATTACK_THRESHOLD;
    g_settings.audio_volume     = DEFAULT_VOLUME;
    g_settings.lives_per_level  = LIVES_PER_LEVEL;
    g_settings.input_mode       = DEFAULT_INPUT_MODE;
    g_settings.games_played     = 0;
    g_settings.total_points     = 0;
    g_settings.high_score       = 0;
    g_settings.boss_kills       = 0;
    Serial.println("[settings] defaults loaded");
}

void load() {
    twang_storage::load();
}

void save() {
    twang_storage::save();
}

void dump_serial() {
    Serial.println("\r\n====== TWANG Settings Menu ========");
    Serial.println("=    Current values are shown     =");
    Serial.println("=   Send new values like B=150    =");
    Serial.println("===================================");
    Serial.printf("C=%u  (LED Count %d-%d)\r\n",
                  g_settings.led_count, MIN_LEDS, MAX_LEDS);
    Serial.printf("B=%u  (LED Brightness %d-%d)\r\n",
                  g_settings.led_brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
    Serial.printf("S=%u  (Sound Volume %d-%d)\r\n",
                  g_settings.audio_volume, MIN_VOLUME, MAX_VOLUME);
    Serial.printf("D=%u  (Deadzone %d-%d)\r\n",
                  g_settings.joystick_deadzone,
                  MIN_JOYSTICK_DEADZONE, MAX_JOYSTICK_DEADZONE);
    Serial.printf("A=%u  (Attack threshold %d-%d)\r\n",
                  g_settings.attack_threshold,
                  MIN_ATTACK_THRESHOLD, MAX_ATTACK_THRESHOLD);
    Serial.printf("L=%u  (Lives per level %d-%d)\r\n",
                  g_settings.lives_per_level,
                  MIN_LIVES_PER_LEVEL, MAX_LIVES_PER_LEVEL);
    Serial.println("\r\n? to show, R to reset, P to clear stats");
}

void change(char param_code, uint16_t v) {
    switch (param_code) {
        case 'C':
            g_settings.led_count = constrain(v, MIN_LEDS, (int)MAX_LEDS);
            save();
            break;
        case 'B':
            g_settings.led_brightness = constrain(v, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
            FastLED.setBrightness(g_settings.led_brightness);
            save();
            break;
        case 'S':
            g_settings.audio_volume = constrain(v, MIN_VOLUME, MAX_VOLUME);
            save();
            break;
        case 'D':
            g_settings.joystick_deadzone =
                constrain(v, MIN_JOYSTICK_DEADZONE, MAX_JOYSTICK_DEADZONE);
            save();
            break;
        case 'A':
            g_settings.attack_threshold =
                constrain(v, MIN_ATTACK_THRESHOLD, MAX_ATTACK_THRESHOLD);
            save();
            break;
        case 'L':
            g_settings.lives_per_level =
                constrain(v, MIN_LIVES_PER_LEVEL, MAX_LIVES_PER_LEVEL);
            save();
            break;
        default:
            Serial.printf("[settings] unknown code '%c'\r\n", param_code);
            return;
    }
    dump_serial();
}

static void parse_line() {
    // "C=123\r" style commands
    if (s_read_index < 3 || s_read_buffer[1] != '=') {
        Serial.println("[settings] malformed command");
        s_read_index = 0;
        return;
    }
    char buf[6] = {0};
    for (int i = 0; i < 5 && (i + 2) < s_read_index; ++i) {
        if (!isDigit(s_read_buffer[i + 2])) {
            Serial.println("[settings] non-digit in value");
            s_read_index = 0;
            return;
        }
        buf[i] = s_read_buffer[i + 2];
    }
    change(s_read_buffer[0], atoi(buf));
    memset(s_read_buffer, 0, sizeof(s_read_buffer));
    s_read_index = 0;
}

void handle_serial_char(char c) {
    s_read_buffer[s_read_index] = c;
    switch (c) {
        case '?':
            s_read_index = 0;
            dump_serial();
            return;
        case 'R':
            s_read_index = 0;
            reset_defaults();
            save();
            return;
        case 'P':
            g_settings.games_played = 0;
            g_settings.total_points = 0;
            g_settings.high_score   = 0;
            g_settings.boss_kills   = 0;
            save();
            s_read_index = 0;
            return;
        case '!':
            ESP.restart();
            return;
        default:
            break;
    }
    if (c == '\r') {
        parse_line();
    } else if (++s_read_index >= kReadBufferLen) {
        s_read_index = 0;
    }
}

} // namespace twang_settings
