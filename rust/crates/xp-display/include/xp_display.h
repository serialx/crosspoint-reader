#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Versioned framebuffer-driver ABI. All buffers are borrowed for one call;
// only config.controller_config (optional immutable LUTs) lives until drop.
// Serialize calls and keep the displayed frame available through FINISH.
enum {
  XP_DISPLAY_ABI_VERSION = 1,
  XP_DISPLAY_STATE_BYTES = 64,
  XP_DISPLAY_STATE_ALIGN = 8,
  XP_DISPLAY_UC8253_X3 = 1,
  XP_DISPLAY_OK = 0,
  XP_DISPLAY_INVALID = 1,
  XP_DISPLAY_UNSUPPORTED = 2,
  XP_DISPLAY_FLAG_TURN_OFF = 1,
  XP_DISPLAY_FLAG_FACTORY = 2,
  XP_DISPLAY_RESULT_ASYNC = 1,
  XP_DISPLAY_RESULT_ABORTED = 2,
  XP_DISPLAY_RESULT_COMMITTED = 4,
  XP_DISPLAY_RESULT_MAINTENANCE = 8,
  XP_DISPLAY_CAP_OVERLAY = 1,
  XP_DISPLAY_CAP_ABSOLUTE = 2,
  XP_DISPLAY_CAP_COMBINED = 4,
  XP_DISPLAY_CAP_STRIPS = 8,
  XP_DISPLAY_CAP_ASYNC_BASE = 16,
  XP_DISPLAY_CAP_BUSY_STAGING = 32,
  XP_DISPLAY_CAP_ASYNC_DISPLAY = 64,
  XP_DISPLAY_CAP_STREAM_BW = 128,
  XP_DISPLAY_OP_BEGIN = 0,
  XP_DISPLAY_OP_SLEEP = 1,
  XP_DISPLAY_OP_DISPLAY = 2,
  XP_DISPLAY_OP_DISPLAY_START = 3,
  XP_DISPLAY_OP_DISPLAY_FINISH = 4,
  XP_DISPLAY_OP_DISPLAY_WINDOW = 5,
  XP_DISPLAY_OP_SEED_PREVIOUS = 6,
  XP_DISPLAY_OP_GRAY_BEGIN = 7,
  XP_DISPLAY_OP_GRAY_BASE = 8,
  XP_DISPLAY_OP_GRAY_PRECONDITION = 9,
  XP_DISPLAY_OP_GRAY_LSB = 10,
  XP_DISPLAY_OP_GRAY_MSB = 11,
  XP_DISPLAY_OP_GRAY_STRIP = 12,
  XP_DISPLAY_OP_GRAY_PREPARE = 13,
  XP_DISPLAY_OP_GRAY_DISPLAY = 14,
  XP_DISPLAY_OP_GRAY_CALIBRATE = 15,
  XP_DISPLAY_OP_GRAY_CLEANUP = 16,
  XP_DISPLAY_OP_GRAY_REVERT = 17,
  XP_DISPLAY_OP_RESYNC = 18,
  XP_DISPLAY_OP_SKIP_INITIAL_RESYNC = 19,
  XP_DISPLAY_OP_BACKGROUND = 20,
  XP_DISPLAY_OP_BEGIN_WORK = 21,
  XP_DISPLAY_OP_ABORT_POST = 22,
  XP_DISPLAY_OP_QUERY = 23,
  XP_DISPLAY_OP_MAINTENANCE = 24,
  XP_DISPLAY_OP_IDLE = 25,
  XP_DISPLAY_OP_COMPLETE_NEXT = 26,
  XP_DISPLAY_OP_FULL_COMPLETES = 27,
  XP_DISPLAY_OP_ACCENT = 28,
  XP_DISPLAY_OP_FAST_CUTOFF = 29,
  XP_DISPLAY_OP_HOLD_FULL = 30,
  XP_DISPLAY_OP_CUSTOM_LUT = 31,
  XP_DISPLAY_TAG_NONE = 0,
  XP_DISPLAY_TAG_X3_PON = 1,
  XP_DISPLAY_TAG_X3_POF = 2,
  XP_DISPLAY_TAG_X3_DRF = 3,
  XP_DISPLAY_TAG_X3_DIRECT_GRAY_DRF = 4,
  XP_DISPLAY_TAG_X3_BW_TARGET_DRF = 5,
  XP_DISPLAY_TAG_X3_POWER_DOWN = 6,
};

// Shared registry; append values without changing existing descriptor layouts.
enum {
  XP_DISPLAY_SSD1677 = 2,
  XP_DISPLAY_UC8179 = 3,
  XP_DISPLAY_UC8279_X3 = 4,
  XP_DISPLAY_UC8279_X4 = 5,
  XP_DISPLAY_UC8279C_A4 = 6,
  XP_DISPLAY_SSD2677 = 7,
  XP_DISPLAY_PAPER_MONO = 8,
  XP_DISPLAY_TAG_V1_FIRST = 0,
  XP_DISPLAY_TAG_V1_LAST = 29,
  XP_DISPLAY_TAG_V3_FIRST = 30,
  XP_DISPLAY_TAG_V3_LAST = 59,
  XP_DISPLAY_TAG_V2_FIRST = 60,
  XP_DISPLAY_TAG_V2_LAST = 89,
  XP_DISPLAY_TAG_V4_FIRST = 90,
  XP_DISPLAY_TAG_V4_LAST = 119,
  XP_DISPLAY_TAG_V3_EXTRA_FIRST = 120,
  XP_DISPLAY_TAG_V3_EXTRA_LAST = 159,
  XP_DISPLAY_PLATFORM_VERSION = 1,
  XP_DISPLAY_INIT_OPTIONS_VERSION = 1,
  XP_DISPLAY_ALLOC_PSRAM_THEN_INTERNAL = 0,
  XP_DISPLAY_ALLOC_INTERNAL = 1,
  XP_DISPLAY_ALLOC_PSRAM_ONLY = 2
};

typedef struct {
  uint32_t abi_version, struct_size;
  void* ctx;
  void* (*alloc)(void*, uint32_t size, uint32_t caps);
  void (*free)(void*, void*);
} XpDisplayPlatform;

typedef struct {
  uint32_t abi_version, struct_size, controller_config_version;
  // Optional, retained until drop; never routed through Rust's global allocator.
  const XpDisplayPlatform* platform;
} XpDisplayInitOptions;

typedef struct {
  uint32_t abi_version, struct_size;
  void* ctx;
  void (*cmd)(void*, uint8_t);
  void (*data8)(void*, uint8_t);
  void (*data_span)(void*, const uint8_t*, uint16_t);
  void (*cmd_data)(void*, uint8_t, const uint8_t*, uint16_t);
  void (*txn_begin)(void*);
  void (*txn_cmd)(void*, uint8_t);
  void (*txn_data)(void*, uint8_t);
  void (*txn_write)(void*, const uint8_t*, uint16_t);
  void (*txn_end)(void*);
  void (*reset)(void*, uint16_t);
  void (*reset_level)(void*, uint8_t);
  uint8_t (*busy_level)(void*);
  void (*busy_wait)(void*, uint32_t, uint32_t);
  void (*refresh_wait)(void*, uint32_t);
  void (*delay_ms)(void*, uint32_t);
  void (*delay_us)(void*, uint32_t);
  uint32_t (*clock_ms)(void*);
  uint32_t (*probe)(void*, uint32_t, uint32_t);
  // Optional; text is borrowed, not terminated. No callback may reenter Rust.
  void (*diagnostic)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint8_t*, uint32_t);
} XpDisplayBus;

typedef struct {
  uint32_t abi_version, struct_size, controller, variant;
  uint16_t width, height;
  uint32_t flags;
  const void* controller_config;
  uint32_t controller_config_size;
} XpDisplayConfig;

// mode: Full/Half/Fast=0/1/2, or Overlay/Absolute/Direct=0/1/2 for GRAY_BEGIN.
// GRAY_BEGIN uses arg as fallback refresh; GRAY_STRIP uses mode for Lsb/Msb=0/1.
// Scalar hooks use arg; boolean scalars are 0/1. QUERY returns result bits.
// Rect fields use visible native coordinates. data/previous lengths are bytes.
typedef struct {
  uint32_t struct_size, operation, mode, flags, arg;
  uint16_t x, y, width, height;
  const uint8_t* data;
  uint32_t data_len;
  const uint8_t* previous;
  uint32_t previous_len;
  const uint8_t* lut;
  uint32_t lut_len;
} XpDisplayRequest;

// Optional UC8253 override: six banks (normal, half, fast, full, gc, preBwMid),
// each VCOM/WW/BW/WB/BB. Each nonnull pointer covers lut_len immutable bytes.
// direct is null to disable Direct mode, or points to five such LUT pointers.
typedef struct {
  const uint8_t* banks[6][5];
  const uint8_t* const* direct;
  uint32_t lut_len;
} XpDisplayX3Config;

// Writes default immutable Rust LUT pointers; direct has room for five pointers
// and must outlive config. No copy of the LUT bytes or allocation is made.
void xp_display_x3_default_config(XpDisplayX3Config* config, const uint8_t** direct);

uint32_t xp_display_abi_version(void);
uint32_t xp_display_state_size(uint32_t controller);
uint32_t xp_display_state_align(uint32_t controller);
// Storage must be aligned, writable, exclusive, and remain at this address.
// Init failure leaves it untouched; only successful init may be used/dropped.
uint32_t xp_display_init(void* storage, uint32_t size, const XpDisplayConfig* config);
// Additive v2 init; null options equals v1 init (config version 1, no platform).
uint32_t xp_display_init_v2(void* storage, uint32_t size, const XpDisplayConfig* config,
                            const XpDisplayInitOptions* options);
// May race execute, but not init/drop. Returns the incremented generation.
uint32_t xp_display_abort(void* storage);
uint32_t xp_display_generation(const void* storage);
// Serialized controller-specific operation. Payload is borrowed for the call.
uint32_t xp_display_extra(void* storage, uint32_t operation, const void* payload, uint32_t payload_size,
                          uint32_t payload_version);
void xp_display_drop(void* storage);
uint32_t xp_display_capabilities(const void* storage, uint32_t gray_mode);
// result may be null. Other pointers must be valid; bus is needed for I/O ops.
uint32_t xp_display_execute(void* storage, const XpDisplayBus* bus, const XpDisplayRequest* request, uint32_t* result);
#ifdef __cplusplus
}
#endif
