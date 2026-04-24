#pragma once

// Build-time hardware configuration for the TWANG firmware.
// Anything that is fixed by the physical board lives here; anything a user
// can change at runtime lives in Settings (config/settings.h).

// -- LED strip --------------------------------------------------------------
// NOTE: names are TWANG-prefixed because FastLED reserves DATA_PIN and
// CLOCK_PIN as template-parameter identifiers inside its headers. If we
// used bare DATA_PIN/CLOCK_PIN as macros, the preprocessor would clobber
// FastLED's own declarations and the build would fail.

// Allow build-flag override: -DTWANG_DATA_PIN_OVERRIDE=2
// so multiple PlatformIO envs can target different GPIO pins.
#ifdef TWANG_DATA_PIN_OVERRIDE
  #define TWANG_DATA_PIN       TWANG_DATA_PIN_OVERRIDE
#else
  #define TWANG_DATA_PIN       16   // default for original TWANG32 hardware
#endif
#define TWANG_CLOCK_PIN      17   // unused for WS2812B; reserved for clocked strips

// Virtual world size the game is rendered into and then scaled to the strip.
#define VIRTUAL_LED_COUNT   1000

// Pick one LED type. Default is WS2812B (Neopixel) - what the prototype uses.
#define USE_NEOPIXEL
// #define USE_APA102

#if defined(USE_NEOPIXEL) && defined(USE_APA102)
#error "Both USE_APA102 and USE_NEOPIXEL are defined in hw_config.h; only one allowed"
#endif
#if !defined(USE_NEOPIXEL) && !defined(USE_APA102)
#error "Define USE_APA102 or USE_NEOPIXEL in hw_config.h"
#endif

#ifdef USE_APA102
  #define LED_TYPE               APA102
  #define LED_COLOR_ORDER        BGR
  #define CONVEYOR_BRIGHTNESS    8
  #define LAVA_OFF_BRIGHTNESS    4
  #define MAX_LEDS               VIRTUAL_LED_COUNT
  #define MIN_REDRAW_INTERVAL    (1000.0f / 60.0f)
#endif

#ifdef USE_NEOPIXEL
  #define LED_TYPE               NEOPIXEL
  #define CONVEYOR_BRIGHTNESS    40
  #define LAVA_OFF_BRIGHTNESS    15
  #define MAX_LEDS               288
  #define MIN_REDRAW_INTERVAL    (1000.0f / 60.0f)
#endif

// -- Audio (optional on-device SFX) ----------------------------------------
#define DAC_AUDIO_PIN          25

// -- MPU6050 (optional, used in fallback mode) -----------------------------
#define MPU_I2C_SDA            21
#define MPU_I2C_SCL            22

// -- Firmware identity -----------------------------------------------------
#define TWANG_FW_MAJOR         0
#define TWANG_FW_MINOR         1
#define TWANG_FW_PATCH         6

// Multiplayer fairness. Single-player stays at 100%; each additional player
// increases enemy speed and spawner cadence by this percentage at level start.
#define TWANG_MULTIPLAYER_SCALE_STEP_PERCENT 12
#define TWANG_MIN_SPAWNER_RATE_MS 250

// Bring-up: set to 1, rebuild, flash - 115200 serial. Default 0 = no overhead.
#ifndef TWANG_TRACE_GAME_INPUT
#define TWANG_TRACE_GAME_INPUT 0
#endif
#ifndef TWANG_TRACE_CONTROL_SPACING
#define TWANG_TRACE_CONTROL_SPACING 0
#endif

// Joystick world parameters (kept identical to the original project).
#define JOYSTICK_ORIENTATION   1
#define JOYSTICK_DIRECTION     1
#define DIRECTION              1
#define USE_GRAVITY            0
#define BEND_POINT             550
