#pragma once
// DAC-based square-wave tone generator. Kept compatible with the original
// TWANG32 sound.h so SFX functions in game_twang.cpp can call into it
// unchanged. A no-speaker board simply produces no audible output.

#include <Arduino.h>
#include <stdint.h>

void sound_init(int pin);
bool sound(uint16_t freq, uint8_t volume);
void soundOff();
void sound_pause();
void sound_resume();
