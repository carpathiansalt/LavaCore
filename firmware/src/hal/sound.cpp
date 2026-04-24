#include "hal/sound.h"

#include <Arduino.h>
#include <esp32-hal-timer.h>

#define ESP32_F_CPU               80000000
#define AUDIO_INTERRUPT_PRESCALER 80
#define SOUND_TIMER_NO            0
#define MIN_FREQ                  20
#define MAX_FREQ                  16000

static hw_timer_t* s_sndTimer       = nullptr;
static volatile bool    s_sound_on       = false;
static volatile bool    s_sound_wave_hi  = true;
static volatile uint8_t s_sound_volume   = 0;
static int      s_dac_pin        = 25;

static void IRAM_ATTR onSoundTimer() {
    if (s_sound_on) {
        dacWrite(s_dac_pin, s_sound_wave_hi ? s_sound_volume : 0);
        s_sound_wave_hi = !s_sound_wave_hi;
    } else {
        dacWrite(s_dac_pin, 0);
    }
}

void sound_init(int pin) {
    s_dac_pin = pin;
    s_sound_on = false;
    pinMode(s_dac_pin, OUTPUT);
    s_sound_volume = 0;

    s_sndTimer = timerBegin(SOUND_TIMER_NO, AUDIO_INTERRUPT_PRESCALER, true);
    timerAttachInterrupt(s_sndTimer, &onSoundTimer, true);
    timerAlarmWrite(s_sndTimer, ESP32_F_CPU / AUDIO_INTERRUPT_PRESCALER / MIN_FREQ, true);
    timerAlarmEnable(s_sndTimer);
}

void sound_pause() {
    if (s_sndTimer != nullptr) timerStop(s_sndTimer);
}

void sound_resume() {
    if (s_sndTimer != nullptr) timerRestart(s_sndTimer);
}

bool sound(uint16_t freq, uint8_t volume) {
    if (volume == 0) { soundOff(); return false; }
    if (freq < MIN_FREQ || freq > MAX_FREQ) return false;
    s_sound_on = true;
    s_sound_volume = volume;
    timerAlarmWrite(s_sndTimer, ESP32_F_CPU / AUDIO_INTERRUPT_PRESCALER / (freq * 2), true);
    return true;
}

void soundOff() {
    s_sound_on = false;
    s_sound_volume = 0;
    if (s_sndTimer != nullptr) {
        timerAlarmWrite(s_sndTimer, ESP32_F_CPU / AUDIO_INTERRUPT_PRESCALER / MIN_FREQ, true);
    }
}
