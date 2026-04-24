#include "input/mpu_input.h"

#include <Arduino.h>
#include <Wire.h>
#include <RunningMedian.h>

#include "config/hw_config.h"
#include "config/settings.h"
#include "game/twang_mpu.h"

namespace {
Twang_MPU       s_mpu;
RunningMedian   s_angle_samples(5);
RunningMedian   s_wobble_samples(5);
} // namespace

void MpuInput::begin() {
    Wire.begin(MPU_I2C_SDA, MPU_I2C_SCL);
    Wire.setClock(100000);

    // Probe the I2C bus for the MPU6050 (0x68). If no device ACKs, we mark
    // the sensor absent and silently no-op updates so the firmware can run
    // headless with only BLE remote input (no I2C error spam).
    Wire.beginTransmission(0x68);
    const uint8_t probe_err = Wire.endTransmission();
    if (probe_err != 0) {
        _initialized = false;
        Serial.printf("[input] MPU6050 not found on I2C (err=%u); disabling onboard sensor fallback\r\n", probe_err);
        return;
    }

    s_mpu.initialize();
    _initialized = true;
    Serial.println("[input] MPU6050 initialized");
}

void MpuInput::update() {
    if (!_initialized) {
        _tilt = 0;
        _wobble = 0;
        return;
    }
    // Direct port of the original getInput() from TWANG32.ino.
    int16_t ax, ay, az, gx, gy, gz;
    s_mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    int a = (JOYSTICK_ORIENTATION == 0 ? ax :
             JOYSTICK_ORIENTATION == 1 ? ay : az) / 166;
    int g = (JOYSTICK_ORIENTATION == 0 ? gx :
             JOYSTICK_ORIENTATION == 1 ? gy : gz);

#ifdef JOYSTICK_DEBUG
    Serial.printf("mpu a:%d g:%d\r\n", a, g);
#endif

    if (abs(a) < g_settings.joystick_deadzone) a = 0;
    if (a > 0) a -= g_settings.joystick_deadzone;
    if (a < 0) a += g_settings.joystick_deadzone;

    s_angle_samples.add(a);
    s_wobble_samples.add(g);

    _tilt = s_angle_samples.getMedian();
    if (JOYSTICK_DIRECTION == 1) _tilt = -_tilt;

    _wobble = abs(static_cast<int>(s_wobble_samples.getHighest()));
}
