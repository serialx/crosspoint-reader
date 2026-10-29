#include "HalTiltSensor.h"

#include <Logging.h>

#include <cstring>

HalTiltSensor halTiltSensor;

XpHalTiltServices HalTiltSensor::services() {
  return {this,
          [](void* ctx, XpHalImuOp op) {
            auto& imu = static_cast<HalTiltSensor*>(ctx)->_sdkImu;
            switch (op) {
              case XP_HAL_IMU_BEGIN:
                return imu.begin();
              case XP_HAL_IMU_SLEEP:
                return imu.sleep();
              case XP_HAL_IMU_WAKE:
                return imu.wake();
              case XP_HAL_IMU_HAS_GYRO:
                return imu.hasGyroscope();
            }
            return false;
          },
          [](void* ctx, float* x, float* y) {
            Imu::Sample sample;
            if (!static_cast<HalTiltSensor*>(ctx)->_sdkImu.read(sample)) return false;
            *x = sample.gx;
            *y = sample.gy;
            return true;
          },
          []() { return static_cast<uint32_t>(millis()); },
          [](void*, uint32_t event, uint32_t bits, uint32_t, uint32_t, uint32_t) {
            float axis;
            memcpy(&axis, &bits, sizeof(axis));
            switch (event) {
              case 0:
                LOG_ERR("GYR", "SDK IMU not found");
                break;
              case 1:
                LOG_ERR("GYR", "IMU standby failed");
                break;
              case 2:
                LOG_INF("GYR", "IMU has no gyroscope");
                break;
              case 3:
                LOG_INF("GYR", "SDK IMU initialized");
                break;
              case 4:
                LOG_ERR("GYR", "IMU wake failed");
                break;
              case 5:
                LOG_ERR("GYR", "IMU sleep failed");
                break;
              case 6:
                LOG_INF("GYR", "Forward Trigger=(%.1f) dps", axis);
                break;
              case 7:
                LOG_INF("GYR", "Backward Trigger=(%.1f) dps", axis);
                break;
            }
          }};
}
void HalTiltSensor::begin() {
  const auto s = services();
  xp_hal_tilt_begin(&state, &s);
}
bool HalTiltSensor::wake() {
  const auto s = services();
  return xp_hal_tilt_wake(&state, &s);
}
bool HalTiltSensor::deepSleep() {
  const auto s = services();
  return xp_hal_tilt_sleep(&state, &s);
}
void HalTiltSensor::update(uint8_t mode, uint8_t orientation, bool inReader) {
  const auto s = services();
  xp_hal_tilt_update(&state, &s, mode, orientation, inReader);
}
bool HalTiltSensor::wasTiltedForward() { return xp_hal_tilt_take(&state, 0); }
bool HalTiltSensor::wasTiltedBack() { return xp_hal_tilt_take(&state, 1); }
bool HalTiltSensor::hadActivity() { return xp_hal_tilt_take(&state, 2); }
void HalTiltSensor::clearPendingEvents() { xp_hal_tilt_clear(&state); }
