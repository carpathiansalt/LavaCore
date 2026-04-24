#include "engine/mode_manager.h"

#include <Arduino.h>

#include "apps/app_ambient.h"
#include "apps/game_twang.h"
#include "input/input_manager.h"

namespace {
GameTwang   s_game;
AppAmbient  s_ambient;
IApp*       s_current      = nullptr;
ModeId      s_current_mode = MODE_IDLE;

IApp* app_for(ModeId m) {
    switch (m) {
        case MODE_GAME_TWANG: return &s_game;
        case MODE_AMBIENT:    return &s_ambient;
        default:              return nullptr;
    }
}

void switch_to(ModeId m) {
    if (m == s_current_mode) return;
    IApp* next = app_for(m);
    if (next == nullptr) return;
    s_current      = next;
    s_current_mode = m;
    s_current->init();
    Serial.printf("[mode] switched to mode_id=%u\r\n", m);
}

} // namespace

namespace mode_manager {

void begin() {
    app_ambient::begin();
    switch_to(MODE_GAME_TWANG);
}

void tick(uint32_t now_ms) {
    input_manager::tick();
    if (s_current != nullptr) s_current->tick(now_ms);
}

ModeId current_mode() { return s_current_mode; }

bool request_mode(ModeId mode, uint8_t /*submode*/) {
    if (app_for(mode) == nullptr) return false;
    switch_to(mode);
    return true;
}

void fill_telemetry(TelemetryPacket& out) {
    if (s_current != nullptr) s_current->fill_telemetry(out);
    else {
        out.mode_id = MODE_IDLE;
        out.global_flags = 0;
    }
}

} // namespace mode_manager
