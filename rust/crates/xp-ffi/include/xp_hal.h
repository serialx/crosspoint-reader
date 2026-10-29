#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*XpHalDiagnostic)(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d);

// All services are borrowed for one synchronous call. Callers retain SDK objects
// and hold the existing locks; callbacks must not retain Rust state pointers.
// Non-null state/output pointers must be aligned, valid, and disjoint. Every
// required service callback must be non-null; diagnostic callbacks may be null.
// Secret output has 32 writable bytes. Power operations acquire their supplied
// mutex before borrowing shared state; battery state is separately owned.
typedef struct {
  uint8_t bytes[32];
  bool loaded;
} XpHalSecret;
typedef struct {
  void* ctx;
  bool (*open)(void*);
  // 0: exact stored secret, 1: missing, 2: malformed/read error.
  uint8_t (*read)(void*, uint8_t*);
  void (*random)(void*, uint8_t*);
  bool (*write)(void*, const uint8_t*);
  bool (*commit)(void*);
  void (*close)(void*);
} XpHalSecretServices;
bool xp_hal_secret(XpHalSecret*, const XpHalSecretServices*, uint8_t* out);

typedef struct {
  int64_t (*now)(void);
  int64_t (*read_floor)(void);
  void (*write_floor)(int64_t);
  int64_t (*raise_floor)(int64_t);
  void (*set_time)(int64_t);
} XpHalTimeServices;
int64_t xp_hal_time_raise(int64_t* floor, int64_t value);
bool xp_hal_time_init(const XpHalTimeServices*);
void xp_hal_time_note(const XpHalTimeServices*);
void xp_hal_time_adopt(const XpHalTimeServices*, int64_t epoch);
int64_t xp_hal_time_now(const XpHalTimeServices*);
bool xp_hal_time_wait(uint32_t deadline, uint32_t now);

typedef struct {
  uint8_t brightness;
  bool lit;
} XpHalLight;
// Returns true when the caller must send brightness to the SDK.
bool xp_hal_light_brightness(XpHalLight*, uint8_t percent);
bool xp_hal_light_on(XpHalLight*, bool on);
uint8_t xp_hal_percent(uint8_t percent);
uint8_t xp_hal_light_output(const XpHalLight*);

typedef struct {
  uint32_t init_ms, last_poll_ms, last_tilt_ms, wake_ms;
  bool available, forward, back, activity, in_tilt, awake;
} XpHalTilt;
typedef enum { XP_HAL_IMU_BEGIN, XP_HAL_IMU_SLEEP, XP_HAL_IMU_WAKE, XP_HAL_IMU_HAS_GYRO } XpHalImuOp;
typedef struct {
  void* ctx;
  bool (*operation)(void*, XpHalImuOp);
  bool (*read)(void*, float*, float*);
  uint32_t (*millis)(void);
  // Events 0..5: begin/standby/no-gyro/ready/wake/sleep; 6/7: forward/back.
  XpHalDiagnostic diagnostic;
} XpHalTiltServices;
void xp_hal_tilt_begin(XpHalTilt*, const XpHalTiltServices*);
bool xp_hal_tilt_wake(XpHalTilt*, const XpHalTiltServices*);
bool xp_hal_tilt_sleep(XpHalTilt*, const XpHalTiltServices*);
void xp_hal_tilt_update(XpHalTilt*, const XpHalTiltServices*, uint8_t mode, uint8_t orientation, bool in_reader);
bool xp_hal_tilt_take(XpHalTilt*, uint8_t event);
void xp_hal_tilt_clear(XpHalTilt*);

typedef struct {
  int64_t utc;
  uint32_t last_poll_ms;
  bool available, cached;
} XpHalClock;
typedef struct {
  uint16_t year;
  uint8_t month, day, hour, minute, second;
} XpHalCalendar;
typedef struct {
  void* ctx;
  bool (*read)(void*, XpHalCalendar*);
  uint32_t (*millis)(void);
} XpHalClockServices;
int64_t xp_hal_clock_epoch(const XpHalCalendar*);
bool xp_hal_clock_read(XpHalClock*, const XpHalClockServices*, int64_t* utc);
bool xp_hal_clock_format(char* out, size_t size, bool twelve, uint8_t hour, uint8_t minute);

typedef struct {
  int32_t normal_freq;
  bool low_power, locked;
} XpHalPower;
typedef struct {
  int32_t battery_percent;
  uint32_t battery_poll_ms;
} XpHalBattery;
typedef enum { XP_HAL_POWER_WIFI, XP_HAL_POWER_LOCK, XP_HAL_POWER_UNLOCK, XP_HAL_POWER_FREQUENCY } XpHalPowerOp;
typedef struct {
  void* ctx;
  int32_t (*operation)(void*, XpHalPowerOp, int32_t);
  // 0: enter low; 1: restore normal; 2: set failed; 3: lock already held.
  XpHalDiagnostic diagnostic;
  int32_t low_frequency;
} XpHalPowerServices;
void xp_hal_power_set(XpHalPower*, const XpHalPowerServices*, bool enabled);
bool xp_hal_power_lock(XpHalPower*, const XpHalPowerServices*);
void xp_hal_power_unlock(XpHalPower*, const XpHalPowerServices*, bool valid);
typedef struct {
  void* ctx;
  uint32_t (*millis)(void);
  bool (*read_checked)(void*, uint16_t*);
  uint16_t (*read)(void*);
} XpHalBatteryServices;
uint16_t xp_hal_power_battery(XpHalBattery*, const XpHalBatteryServices*, bool gauge);

typedef struct {
  void* ctx;
  uint8_t (*read)(bool cached);
  void (*write)(uint8_t value);
  uint8_t (*probe)(uint8_t* first, uint8_t* second);
  XpHalDiagnostic diagnostic;
} XpHalDetectServices;
bool xp_hal_detect_x3(const XpHalDetectServices*);
typedef struct {
  bool usb_connected, usb_changed;
  uint8_t page_buttons;
  bool page_pressed;
} XpHalInput;
void xp_hal_input_update(XpHalInput*, uint8_t buttons, bool usb);
bool xp_hal_input_adc(int32_t first, int32_t second);
uint32_t xp_hal_wake_reason(bool deep_reset, bool button_wake, bool undefined_wake, bool power_reset,
                            bool unknown_reset, bool usb, bool cold_button);
typedef struct {
  void* ctx;
  bool (*pressed)(void*);
  bool (*debounce)(void*);
  void (*update)(void*);
  uint32_t (*millis)(void);
  void (*delay)(uint32_t);
} XpHalWakeServices;
bool xp_hal_verify_wake(const XpHalWakeServices*);

typedef enum {
  XP_HAL_DISPLAY_X3,
  XP_HAL_DISPLAY_BEGIN,
  XP_HAL_DISPLAY_SKIP,
  XP_HAL_DISPLAY_WAKE,
  XP_HAL_DISPLAY_RESYNC,
  XP_HAL_DISPLAY_BUFFER,
  XP_HAL_DISPLAY_ASYNC,
  XP_HAL_DISPLAY_REFRESH,
  XP_HAL_DISPLAY_GRAY,
  XP_HAL_DISPLAY_GRAY_MODE
} XpHalDisplayOp;
typedef uint32_t (*XpHalDisplayCall)(void*, XpHalDisplayOp, uint32_t, bool, uint32_t);
void xp_hal_display_begin(void*, XpHalDisplayCall, bool x3, bool seamless);
bool xp_hal_display_refresh(void*, XpHalDisplayCall, bool x3, XpHalDisplayOp, uint32_t refresh, bool off,
                            uint32_t gray_mode);

typedef struct {
  void* ctx;
  bool (*open)(void*);
  bool (*is_directory)(void*);
  size_t (*size)(void*);
  size_t (*largest_block)(void*);
  int32_t (*read_string)(void*, size_t);
} XpHalReadServices;
// 0: failure, 1: complete, 2: insufficient contiguous heap (caller logs size).
uint32_t xp_hal_read_string(const XpHalReadServices*, size_t cap);
typedef struct XpHalBlockDevice XpHalBlockDevice;
typedef struct {
  void* ctx;
  XpHalBlockDevice* (*detach)(void*);
  bool (*begin)(void*, XpHalBlockDevice*);
  bool (*remount)(void*);
  XpHalDiagnostic diagnostic;
} XpHalUsbServices;
bool xp_hal_usb_begin(const XpHalUsbServices*);

typedef enum {
  XP_HAL_HAPTIC_ENABLE,
  XP_HAL_HAPTIC_INTENSITY,
  XP_HAL_HAPTIC_BEGIN,
  XP_HAL_HAPTIC_PULSE,
  XP_HAL_HAPTIC_PATTERN
} XpHalHapticOp;
typedef bool (*XpHalHapticCall)(XpHalHapticOp, uint8_t);
void xp_hal_haptic(XpHalHapticCall, bool long_press, bool enabled, bool pressed, uint8_t intensity);

typedef enum {
  XP_HAL_SYSTEM_PANIC,
  XP_HAL_SYSTEM_CLEAR,
  XP_HAL_SYSTEM_SANITIZE,
  XP_HAL_SYSTEM_CLEAR_LOGS,
  XP_HAL_SYSTEM_INFO,
  XP_HAL_SYSTEM_OPEN,
  XP_HAL_SYSTEM_WRITE,
  XP_HAL_SYSTEM_CLOSE,
  XP_HAL_SYSTEM_CONSUME,
  XP_HAL_SYSTEM_SIZE
} XpHalSystemOp;
typedef size_t (*XpHalSystemCall)(void*, XpHalSystemOp);
void xp_hal_system_begin(void*, XpHalSystemCall);
// 0: no panic; 1: saved; 2: short write; 3: open failed.
uint32_t xp_hal_system_save(void*, XpHalSystemCall, size_t* written);
bool xp_hal_system_is_panic(bool panic, bool watchdog, uint32_t marker);

typedef struct {
  bool (*completed)(void);
  uint32_t (*millis)(void);
  void (*delay)(uint32_t);
} XpHalSyncServices;
bool xp_hal_time_sync(const XpHalTimeServices*, const XpHalSyncServices*, uint32_t timeout_ms);
typedef struct {
  void* ctx;
  bool (*wifi)(void*);
  void (*configure)(void*);
  bool (*completed)(void);
  void (*delay)(uint32_t);
  bool (*write_rtc)(void*, XpHalCalendar*);
  void (*restore_timezone)(void*);
  // 0: no WiFi; 1: starting; 2: set; 3: timeout.
  XpHalDiagnostic diagnostic;
} XpHalClockSyncServices;
bool xp_hal_clock_sync(XpHalClock*, const XpHalClockSyncServices*);

#ifdef __cplusplus
}
#endif
