#pragma once
// A minimal MPU-6050 helper limited to what TWANG needs. Compatible with the
// interface used by the original project.
//  B. Dring 2/2018 — kept intact.
#include <Arduino.h>
#include <Wire.h>

class Twang_MPU {
  public:
    void initialize();
    void getMotion6(int16_t* xAccel, int16_t* yAccel, int16_t* zAccel,
                    int16_t* xGyro,  int16_t* yGyro,  int16_t* zGyro);
    bool verify();

  private:
    static const uint8_t MPU_ADDR             = 0x68;
    static const uint8_t PWR_MGMT_1           = 0x6B;
    static const uint8_t MPU_DATA_REG_START   = 0x3B;
    static const uint8_t MPU_DATA_LEN         = 14;
    static const uint8_t MPU_DATA_WHO_AM_I    = 0x75;
};

inline void Twang_MPU::initialize() {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(PWR_MGMT_1);
    Wire.write(0);
    Wire.endTransmission(true);
}

inline bool Twang_MPU::verify() {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(MPU_DATA_WHO_AM_I);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, (uint8_t)1, (uint8_t)true);
    return (Wire.read() == MPU_ADDR);
}

inline void Twang_MPU::getMotion6(int16_t* xAccel, int16_t* yAccel, int16_t* zAccel,
                                  int16_t* xGyro,  int16_t* yGyro,  int16_t* zGyro) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(MPU_DATA_REG_START);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, MPU_DATA_LEN, (uint8_t)true);
    *xAccel = Wire.read() << 8 | Wire.read();
    *yAccel = Wire.read() << 8 | Wire.read();
    *zAccel = Wire.read() << 8 | Wire.read();
    Wire.read(); Wire.read(); // temperature (discarded)
    *xGyro  = Wire.read() << 8 | Wire.read();
    *yGyro  = Wire.read() << 8 | Wire.read();
    *zGyro  = Wire.read() << 8 | Wire.read();
}
