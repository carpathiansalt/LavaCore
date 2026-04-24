#pragma once
#include <FastLED.h>

#include "config/hw_config.h"

// Global LED buffer. Sized to MAX_LEDS (strip-type dependent); only the
// first g_settings.led_count entries are actually pushed to hardware.
extern CRGB g_leds[MAX_LEDS];

namespace led_output {

// Configures FastLED and spawns the core-0 show task. Call once from setup().
void begin();

// Triggers a FastLED.show() from any core. Blocks until the show completes.
void show();

} // namespace led_output
