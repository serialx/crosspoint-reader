#pragma once
#include <Arduino.h>
inline bool imuBegin = true, imuSleep = true, imuWake = true, imuRead = true, imuGyro = true;
inline float sampleX = 0, sampleY = 0;
struct Imu {
  struct Sample {
    float gx, gy, gz;
  };
  bool begin() {
    puts("imu begin");
    return imuBegin;
  }
  bool sleep() {
    puts("imu sleep");
    return imuSleep;
  }
  bool wake() {
    puts("imu wake");
    return imuWake;
  }
  bool hasGyroscope() {
    puts("imu gyro");
    return imuGyro;
  }
  bool read(Sample& s) {
    puts("imu read");
    s = {sampleX, sampleY, 0};
    return imuRead;
  }
};
