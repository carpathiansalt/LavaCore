#include "apps/app_ambient.h"

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>

#include "config/hw_config.h"
#include "config/settings.h"
#include "hal/led_output.h"

// ==========================================================================
// Ambient mode. Owns its own tiny state blob (independent of Settings) so
// the phone can tweak preset / color / speed frequently without churning
// the main NVS namespace. Persisted under "ambient/state_v1".
// ==========================================================================

namespace {

constexpr char kNvsNamespace[] = "ambient";
constexpr char kNvsKey[]       = "state_v1";
constexpr uint8_t kSchema      = 1;

struct State {
    uint8_t  schema      = kSchema;
    uint8_t  preset      = AMBIENT_RAINBOW;
    uint8_t  r           = 255;
    uint8_t  g           = 64;
    uint8_t  b           = 0;
    uint8_t  brightness  = 180;
    uint8_t  speed       = 128;
    uint8_t  enabled     = 1;
};

State       s_state;
bool        s_first_tick_since_init = true;

// Animation scratch.
uint8_t   s_fire_heat[MAX_LEDS] = {0};
uint16_t  s_chase_pos           = 0;
uint32_t  s_last_step_ms        = 0;

// Speed scaling: map speed byte 0..255 to a frame period in ms for animations
// that step discretely (chase / sparkle). 0 = no motion (1000 ms), 255 ~ 10 ms.
uint16_t speed_to_period_ms(uint8_t speed) {
    if (speed == 0) return 1000;
    return static_cast<uint16_t>(1000u - (static_cast<uint32_t>(speed) * 990u / 255u));
}

void render_solid() {
    const CRGB c(s_state.r, s_state.g, s_state.b);
    for (int i = 0; i < g_settings.led_count; ++i) g_leds[i] = c;
}

void render_breathing() {
    // Phase in [0..TAU]; brightness = (1 + sin(phase)) / 2.
    float phase = (millis() % 4000u) / 4000.0f * TWO_PI;
    float w     = 0.5f + 0.5f * sinf(phase);
    // Map speed so higher speed -> faster breathing.
    phase *= (0.5f + (s_state.speed / 255.0f) * 1.5f);
    w = 0.2f + 0.8f * (0.5f + 0.5f * sinf(phase));

    uint8_t r = static_cast<uint8_t>(s_state.r * w);
    uint8_t g = static_cast<uint8_t>(s_state.g * w);
    uint8_t b = static_cast<uint8_t>(s_state.b * w);
    CRGB c(r, g, b);
    for (int i = 0; i < g_settings.led_count; ++i) g_leds[i] = c;
}

void render_rainbow() {
    // Continuous moving rainbow. `offset` grows with time scaled by speed.
    static uint32_t s_phase = 0;
    s_phase += (1u + static_cast<uint32_t>(s_state.speed) / 16u);
    const uint8_t startHue = static_cast<uint8_t>(s_phase & 0xFF);
    const uint8_t delta    = 255 / _max(g_settings.led_count, 1);
    fill_rainbow(g_leds, g_settings.led_count, startHue, delta ? delta : 1);
}

void render_fire() {
    static constexpr uint8_t COOLING  = 75;
    static constexpr uint8_t SPARKING = 40;
    for (int i = 0; i < g_settings.led_count; ++i) {
        s_fire_heat[i] = qsub8(s_fire_heat[i],
                               random8(0, ((COOLING * 10) / g_settings.led_count) + 2));
    }
    for (int k = g_settings.led_count - 1; k >= 2; --k) {
        s_fire_heat[k] = (s_fire_heat[k-1] + s_fire_heat[k-2] + s_fire_heat[k-2]) / 3;
    }
    if (random8() < SPARKING) {
        int y = random8(7);
        s_fire_heat[y] = qadd8(s_fire_heat[y], random8(160, 255));
    }
    // Tint the heat gradient using the user-chosen primary color so the
    // flame matches the ambient palette. Heat 0..255 => brightness scale.
    for (int j = 0; j < g_settings.led_count; ++j) {
        uint8_t h = s_fire_heat[j];
        g_leds[j] = CRGB(
            (static_cast<uint16_t>(s_state.r) * h) / 255,
            (static_cast<uint16_t>(s_state.g) * h) / 255,
            (static_cast<uint16_t>(s_state.b) * h) / 255);
    }
}

void render_chase() {
    fadeToBlackBy(g_leds, g_settings.led_count, 40);
    const uint32_t now = millis();
    const uint16_t period = speed_to_period_ms(s_state.speed);
    if (now - s_last_step_ms >= period / 8) {   // 8 leds per cycle
        s_last_step_ms = now;
        s_chase_pos = (s_chase_pos + 1) % g_settings.led_count;
    }
    g_leds[s_chase_pos] = CRGB(s_state.r, s_state.g, s_state.b);
}

void render_sparkle() {
    fadeToBlackBy(g_leds, g_settings.led_count, 48);
    // Proportional to speed: up to N new sparkles per frame.
    const uint8_t sparkles = 1 + (s_state.speed / 64);   // 1..4
    for (uint8_t i = 0; i < sparkles; ++i) {
        int pos = random16(g_settings.led_count);
        g_leds[pos] += CRGB(s_state.r, s_state.g, s_state.b);
    }
}

void render_current_preset() {
    switch (s_state.preset) {
        case AMBIENT_SOLID:     render_solid();     break;
        case AMBIENT_BREATHING: render_breathing(); break;
        case AMBIENT_RAINBOW:   render_rainbow();   break;
        case AMBIENT_FIRE:      render_fire();      break;
        case AMBIENT_CHASE:     render_chase();     break;
        case AMBIENT_SPARKLE:   render_sparkle();   break;
        default:                render_rainbow();
    }
}

void load_from_nvs() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) {
        Serial.println("[ambient] NVS open failed, using defaults");
        return;
    }
    const size_t len = prefs.getBytesLength(kNvsKey);
    if (len == sizeof(State)) {
        State tmp{};
        prefs.getBytes(kNvsKey, &tmp, sizeof(tmp));
        if (tmp.schema == kSchema) {
            s_state = tmp;
        }
    }
    prefs.end();
    Serial.printf("[ambient] loaded preset=%u rgb=(%u,%u,%u) br=%u speed=%u\r\n",
                  s_state.preset, s_state.r, s_state.g, s_state.b,
                  s_state.brightness, s_state.speed);
}

void save_to_nvs() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return;
    prefs.putBytes(kNvsKey, &s_state, sizeof(s_state));
    prefs.end();
}

} // namespace

// -- IApp implementation ---------------------------------------------------

void AppAmbient::init() {
    s_first_tick_since_init = true;
    s_last_step_ms          = 0;
    s_chase_pos             = 0;
    memset(s_fire_heat, 0, sizeof(s_fire_heat));
}

void AppAmbient::tick(uint32_t /*now_ms*/) {
    if (s_first_tick_since_init) {
        FastLED.setBrightness(s_state.brightness);
        s_first_tick_since_init = false;
    }
    if (s_state.enabled == 0) {
        FastLED.clear();
        led_output::show();
        return;
    }
    render_current_preset();
    led_output::show();
}

void AppAmbient::fill_telemetry(TelemetryPacket& out) const {
    out.mode_id = MODE_AMBIENT;
    out.player_count = 0;
    out.global_flags = 0;
    // Per-player block stays zeroed (no game session in ambient).
    for (auto& p : out.players) { p.lives = 0; p.flags = 0; p.score_x10 = 0; }
}

// -- Public API ------------------------------------------------------------

namespace app_ambient {

void begin() {
    load_from_nvs();
}

AmbientPacket snapshot() {
    AmbientPacket pkt{};
    pkt.version    = 1;
    pkt.preset_id  = s_state.preset;
    pkt.r          = s_state.r;
    pkt.g          = s_state.g;
    pkt.b          = s_state.b;
    pkt.brightness = s_state.brightness;
    pkt.speed      = s_state.speed;
    pkt.flags      = (s_state.enabled ? AMB_ENABLED : 0) | AMB_PERSIST;
    pkt.reserved   = 0;
    return pkt;
}

void apply(const AmbientPacket& pkt) {
    uint8_t preset = pkt.preset_id;
    if (preset >= AMBIENT_PRESET_COUNT) preset = AMBIENT_RAINBOW;
    s_state.preset     = preset;
    s_state.r          = pkt.r;
    s_state.g          = pkt.g;
    s_state.b          = pkt.b;
    s_state.brightness = pkt.brightness;
    s_state.speed      = pkt.speed;
    s_state.enabled    = (pkt.flags & AMB_ENABLED) ? 1 : 0;

    // Apply brightness immediately so the live preview feels snappy.
    FastLED.setBrightness(s_state.brightness);

    if (pkt.flags & AMB_PERSIST) save_to_nvs();

    Serial.printf("[ambient] apply preset=%u rgb=(%u,%u,%u) br=%u sp=%u en=%u\r\n",
                  s_state.preset, s_state.r, s_state.g, s_state.b,
                  s_state.brightness, s_state.speed, s_state.enabled);
}

} // namespace app_ambient
