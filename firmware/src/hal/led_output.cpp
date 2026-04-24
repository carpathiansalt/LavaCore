#include "hal/led_output.h"

#include <Arduino.h>

#include "config/hw_config.h"
#include "config/settings.h"

CRGB g_leds[MAX_LEDS];

namespace {

// The FastLED show task runs on core 0 because the RMT peripheral used by
// WS2812B is sensitive to core-switching and WiFi/BLE disabling interrupts
// on core 0.
constexpr int kFastLedShowCore = 0;

TaskHandle_t s_show_task_handle = nullptr;
TaskHandle_t s_user_task_handle = nullptr;

void fast_led_show_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        FastLED.show();
        xTaskNotifyGive(s_user_task_handle);
    }
}

} // namespace

namespace led_output {

void begin() {
#ifdef USE_NEOPIXEL
    Serial.println("[led] compiled for WS2812B");
    FastLED.addLeds<LED_TYPE, TWANG_DATA_PIN>(g_leds, MAX_LEDS);
#endif
#ifdef USE_APA102
    Serial.println("[led] compiled for APA102");
    FastLED.addLeds<LED_TYPE, TWANG_DATA_PIN, TWANG_CLOCK_PIN, LED_COLOR_ORDER>(g_leds, MAX_LEDS);
#endif
    FastLED.setBrightness(g_settings.led_brightness);
    FastLED.setDither(1);

    xTaskCreatePinnedToCore(fast_led_show_task, "fastled_show_task",
                            2048, nullptr, 2, &s_show_task_handle,
                            kFastLedShowCore);
}

void show() {
    if (s_user_task_handle == nullptr) {
        s_user_task_handle = xTaskGetCurrentTaskHandle();
        xTaskNotifyGive(s_show_task_handle);
        const TickType_t timeout = pdMS_TO_TICKS(200);
        ulTaskNotifyTake(pdTRUE, timeout);
        s_user_task_handle = nullptr;
    }
}

} // namespace led_output
