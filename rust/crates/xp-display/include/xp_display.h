#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Versioned framebuffer-driver ABI. Bus/request descriptors and frame spans are
// borrowed for one call. Keep state, controller config/LUTs, platform table/context
// and retained scratch valid until drop; retained allocations are released there.
// Serialize calls except the atomic abort/generation APIs. Keep the displayed
// frame available through FINISH.
enum {
  XP_DISPLAY_ABI_VERSION = 1,
  XP_DISPLAY_STATE_BYTES = 64,
  XP_DISPLAY_STATE_ALIGN = 8,
  // Factory capacities are checked against each Rust state on the target.
  XP_DISPLAY_UC8253_X3_STATE_BYTES = 16 + sizeof(void*),
  XP_DISPLAY_SSD1677_STATE_BYTES = 16 + 2 * sizeof(void*),
  XP_DISPLAY_UC8279_X3_STATE_BYTES = 16,
  XP_DISPLAY_UC8179_STATE_BYTES = 16 + 4 * sizeof(void*),
  XP_DISPLAY_UC8279_X4_STATE_BYTES = XP_DISPLAY_UC8179_STATE_BYTES,
  XP_DISPLAY_UC8279C_A4_STATE_BYTES = 8 + 7 * sizeof(void*),
  XP_DISPLAY_UC8253_X3 = 1,
  XP_DISPLAY_OK = 0,
  XP_DISPLAY_INVALID = 1,
  XP_DISPLAY_UNSUPPORTED = 2,
  XP_DISPLAY_FLAG_TURN_OFF = 1,
  XP_DISPLAY_FLAG_FACTORY = 2,
  XP_DISPLAY_FLAG_RESTORE_WINDOW = 4,
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

// SSD1677 config.variant and config.flags. Unknown/custom SDK configurations
// and SSD1677_PROBE_DEBUG keep C++; no controller_config payload is accepted.
enum {
  XP_DISPLAY_SSD1677_X4 = 0,
  XP_DISPLAY_SSD1677_STICKY = 1,
  XP_DISPLAY_SSD1677_METALIO = 2,
  XP_DISPLAY_SSD1677_PICCO_YRD = 3,
  XP_DISPLAY_SSD1677_PICCO_DEPG = 4,
  XP_DISPLAY_SSD1677_PICCO_OPM = 5,
  XP_DISPLAY_SSD1677_MIRROR_X = 1,
  XP_DISPLAY_SSD1677_MIRROR_Y = 2,
  XP_DISPLAY_SSD1677_FAST_DU = 4,
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
  XP_DISPLAY_TAG_SSD_SOFT_RESET = 60,
  XP_DISPLAY_TAG_SSD_AUTO_BW = 61,
  XP_DISPLAY_TAG_SSD_AUTO_RED = 62,
  XP_DISPLAY_TAG_SSD_REFRESH = 63,
  XP_DISPLAY_TAG_SSD_GRAY_POWER_ON = 64,
  XP_DISPLAY_TAG_SSD_POWER_DOWN = 65,
  XP_DISPLAY_TAG_SSD_FACTORY_GRAY = 66,
  XP_DISPLAY_DIAG_SSD_WINDOW_ALLOCATION = 67,
  XP_DISPLAY_TAG_V4_FIRST = 90,
  XP_DISPLAY_TAG_V4_LAST = 119,
  XP_DISPLAY_TAG_V3_EXTRA_FIRST = 120,
  XP_DISPLAY_TAG_V3_EXTRA_LAST = 159,
  XP_DISPLAY_PLATFORM_VERSION = 1,
  XP_DISPLAY_INIT_OPTIONS_VERSION = 1,
  XP_DISPLAY_ALLOC_PSRAM_THEN_INTERNAL = 0,
  XP_DISPLAY_ALLOC_INTERNAL = 1,
  XP_DISPLAY_ALLOC_PSRAM_ONLY = 2,
  XP_DISPLAY_ALLOC_DEFAULT = 3
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
  // Optional only for UC8279C A4, which uses busy_wait for refresh completion.
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

enum {
  XP_DISPLAY_TAG_UC8279_X3_PON = 30,
  XP_DISPLAY_TAG_UC8279_X3_POF = 31,
  XP_DISPLAY_TAG_UC8279_X3_DRF = 32,
  XP_DISPLAY_TAG_UC8279_X3_GRAY_PON = 33,
  XP_DISPLAY_TAG_UC8279_X3_GRAY_DRF = 34,
  XP_DISPLAY_TAG_UC8279_X3_DIRECT_GRAY_DRF = 35,
  XP_DISPLAY_TAG_UC8279_X3_GRAY_POF = 36,
  XP_DISPLAY_TAG_UC8279_X3_BW_TARGET_DRF = 37,
  XP_DISPLAY_TAG_UC8279_X3_POWER_DOWN = 38,
  XP_DISPLAY_TAG_UC8279_X4_PON = 40,
  XP_DISPLAY_TAG_UC8279_X4_POF = 41,
  XP_DISPLAY_TAG_UC8279_X4_DRF = 42,
  XP_DISPLAY_TAG_UC8279_X4_BW_TARGET_DRF = 43,
  XP_DISPLAY_TAG_UC8279_X4_POWER_DOWN = 44,
  XP_DISPLAY_TAG_ABSOLUTE_PLANE = 45,
  XP_DISPLAY_TAG_UC8279_X4_GRAY_PON = 46,
  XP_DISPLAY_TAG_UC8279_X4_GRAY = 47,
  XP_DISPLAY_TAG_UC8279_X4_DIRECT_GRAY_DRF = 48,
  XP_DISPLAY_TAG_UC8279_X4_GRAY_POF = 49,
  XP_DISPLAY_TAG_UC8279_X4_GRAY_BASE_READY = 50,
  XP_DISPLAY_TAG_UC8279_X4_GRAY_BASE_POF = 51,
  XP_DISPLAY_TAG_UC8279_X4_GRAY_PRE_READY = 52,
  XP_DISPLAY_TAG_UC8279_X4_GRAY_PRE_PON = 53,
  XP_DISPLAY_TAG_UC8279_X4_GRAY_PRE_DRF = 54,
  XP_DISPLAY_TAG_A4_POWER_ON = 57,
  XP_DISPLAY_TAG_A4_REFRESH = 58,
  XP_DISPLAY_TAG_A4_POWER_OFF = 59,
  XP_DISPLAY_TAG_UC8179_PON = 120,
  XP_DISPLAY_TAG_UC8179_POF = 121,
  XP_DISPLAY_TAG_UC8179_DRF = 122,
  XP_DISPLAY_TAG_UC8179_BW_TARGET_DRF = 123,
  XP_DISPLAY_TAG_UC8179_POWER_DOWN = 124,
  XP_DISPLAY_TAG_UC8179_DIRECT_SETUP_POF = 125,
  XP_DISPLAY_TAG_UC8179_DIRECT_EXIT_POF = 126,
  XP_DISPLAY_TAG_UC8179_GRAY_BASE_READY = 127,
  XP_DISPLAY_TAG_UC8179_GRAY_BASE_POF = 128,
  XP_DISPLAY_TAG_UC8179_GRAY_PRE_READY = 129,
  XP_DISPLAY_TAG_UC8179_GRAY_PRE_PON = 130,
  XP_DISPLAY_TAG_UC8179_GRAY_PRE_DRF = 131,
  XP_DISPLAY_TAG_UC8179_GRAY_LSB = 132,
  XP_DISPLAY_TAG_UC8179_GRAY_MSB = 133,
  XP_DISPLAY_TAG_UC8179_DIRECT_READY = 134,
  XP_DISPLAY_TAG_UC8179_DIRECT_PON = 135,
  XP_DISPLAY_TAG_UC8179_DIRECT_GRAY_DRF = 136,
  XP_DISPLAY_TAG_UC8179_DIRECT_POF = 137,
  XP_DISPLAY_TAG_UC8179_GRAY_READY = 138,
  XP_DISPLAY_TAG_UC8179_GRAY_PON = 139,
  XP_DISPLAY_TAG_UC8179_GRAY_SPLIT_DRF = 140,
  XP_DISPLAY_TAG_UC8179_ABSOLUTE_POF = 141,
  XP_DISPLAY_TAG_UC8179_GRAY_CLEANUP = 142,
};

// UC8179/UC8279-X4 config version 1; immutable until drop.
typedef struct {
  uint8_t psr0, psr1, pfs, pll, booster[4], gate_scan, ccset;
  uint8_t temp_full, temp_fast, cdi_active, cdi_idle, cdi_fast, power_save;
  uint16_t controller_height, gate_offset;
  uint8_t psram, classic, row_reverse, mirror;
} XpDisplayUltraChipConfig;

// SSD2677 (variant 0 Picco, 1 Sticky) and PaperMono (variant 0 native,
// 1 rotate180), config version 1. Scratch is exclusive internal RAM retained
// until drop: 1024 bytes for SSD2677, 16384 for PaperMono. Never stack storage.
// Platform allocations use PSRAM_ONLY and remain retained until drop.
// PaperMono extra(version=1): 1 = one lightFrames byte, 2 = resetGray (no payload).
enum {
  XP_DISPLAY_SSD2677_STATE_BYTES = 16 + 8 * sizeof(void*),
  XP_DISPLAY_PAPER_MONO_STATE_BYTES = 176 + 18 * sizeof(void*),
  XP_DISPLAY_TAG_SSD2677_RESET = 90,
  XP_DISPLAY_TAG_SSD2677_INIT = 91,
  XP_DISPLAY_TAG_SSD2677_LATCH = 92,
  XP_DISPLAY_TAG_SSD2677_ON = 93,
  XP_DISPLAY_TAG_SSD2677_OFF = 94,
  XP_DISPLAY_TAG_SSD2677_REFRESH = 95,
  XP_DISPLAY_TAG_PAPER_RESET = 96,
  XP_DISPLAY_TAG_PAPER_REFRESH = 97,
  XP_DISPLAY_TAG_PAPER_IDLE = 98,
  XP_DISPLAY_DIAG_PAPER_OTP = 99,
  XP_DISPLAY_DIAG_PAPER_TRI = 100,
  XP_DISPLAY_PAPER_SET_GRAY = 1,
  XP_DISPLAY_PAPER_RESET_GRAY = 2
};
typedef struct {
  uint8_t* scratch;
  uint32_t scratch_size;
  uint32_t serial_log;
} XpDisplayRetainedConfig;

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
// Synchronous frozen scene; paint may rasterize through disjoint Rust APIs.
// Scratch remains exclusive until return. A nonzero paint status aborts upload.
typedef struct {
  uint32_t struct_size;
  void* ctx;
  uint8_t* scratch;
  uint32_t capacity;
  uint16_t rows;
  uint16_t width;
  uint32_t (*paint)(void*, uint8_t*, uint16_t y, uint16_t rows);
} XpDisplaySource;
uint32_t xp_display_execute_source(void* storage, const XpDisplayBus* bus, const XpDisplayRequest* request,
                                   XpDisplaySource* source, uint32_t* result);

#ifdef __cplusplus
}
#endif
