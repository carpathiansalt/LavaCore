#pragma once
#include "input/input_source.h"

class MpuInput final : public IInputSource {
  public:
    void begin();               // Wire.begin + MPU init
    void update() override;
    int  tilt()   const override { return _tilt; }
    int  wobble() const override { return _wobble; }
    bool is_active() const override { return _initialized; }

    // True if the on-board MPU6050 was detected at boot. Used by the BLE
    // slot-assignment logic so AUTO mode can reserve slot 0 for the joystick
    // only when it actually exists.
    bool probed_present() const { return _initialized; }

  private:
    int  _tilt   = 0;
    int  _wobble = 0;
    bool _initialized = false;
};

// Free-function accessor bridging into the singleton MpuInput owned by
// input_manager.cpp. Defined there so code outside the input pipeline can
// query joystick presence without taking a reference to the private instance.
bool mpu_input_probed_present();
