#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
#include <unordered_map>
static std::unordered_map<void*, size_t> liveSizes;
extern "C" size_t xp_test_live_bytes() {
  size_t total = 0;
  for (const auto& entry : liveSizes) total += entry.second;
  return total;
}
#endif

#ifdef XP_FFI_HOST_FAULT_INJECTION
#ifdef XP_PARA_LINK_FAULT_INJECTION
static size_t failArrayBytes = 0;
static size_t arrayFailures = 0;
extern "C" void xp_test_fail_array_bytes(size_t bytes) {
  failArrayBytes = bytes;
  arrayFailures = 0;
}
extern "C" size_t xp_test_array_failures() { return arrayFailures; }
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  if (failArrayBytes && size == failArrayBytes) {
    ++arrayFailures;
    return nullptr;
  }
  return ::operator new[](size);
}
#endif
static size_t liveAllocations = 0;
static size_t allocationAttempts = 0;
extern "C" size_t xp_test_allocation_attempts() { return allocationAttempts; }
extern "C" size_t xp_test_live_allocations() { return liveAllocations; }
static size_t allocationsLeft = SIZE_MAX;
extern "C" void xp_test_fail_allocation_after(size_t count) {
  allocationsLeft = count;
  allocationAttempts = 0;
}
static bool failAllocation() {
  ++allocationAttempts;
  if (allocationsLeft == SIZE_MAX) return false;
  if (allocationsLeft == 0) return true;
  --allocationsLeft;
  return false;
}
#elif defined(XP_FFI_HOST_CSS_FAULT_INJECTION)
static thread_local size_t failAfter = SIZE_MAX;
static thread_local size_t attempts = 0;
extern "C" void cssTestFailAllocation(size_t index) {
  failAfter = index;
  attempts = 0;
}
extern "C" size_t cssTestAllocationAttempts() { return attempts; }
static bool failAllocation() {
  const bool result = attempts == failAfter;
  ++attempts;
  return result;
}
#else
static bool failAllocation() { return false; }
#endif

static void* recordAllocation(void* ptr, size_t size) {
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
  if (ptr) liveSizes[ptr] = size;
#else
  (void)size;
#endif
#ifdef XP_FFI_HOST_FAULT_INJECTION
  if (ptr) ++liveAllocations;
#endif
  return ptr;
}

extern "C" void* heap_caps_malloc(size_t size, uint32_t) {
  return failAllocation() ? nullptr : recordAllocation(std::malloc(size), size);
}
extern "C" void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t) {
  if (failAllocation()) return nullptr;
  void* result = nullptr;
  return posix_memalign(&result, alignment, size) == 0 ? recordAllocation(result, size) : nullptr;
}
extern "C" void* heap_caps_realloc(void* ptr, size_t size, uint32_t) {
  if (failAllocation()) return nullptr;
  const auto address = reinterpret_cast<uintptr_t>(ptr);
  void* result = std::realloc(ptr, size);
  if (!result) return nullptr;
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
  if (address) liveSizes.erase(reinterpret_cast<void*>(address));
  liveSizes[result] = size;
#endif
#ifdef XP_FFI_HOST_FAULT_INJECTION
  if (!address) ++liveAllocations;
#else
  (void)address;
#endif
  return result;
}
extern "C" void heap_caps_free(void* ptr) {
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
  if (ptr) liveSizes.erase(ptr);
#endif
#ifdef XP_FFI_HOST_FAULT_INJECTION
  if (ptr) --liveAllocations;
#endif
  std::free(ptr);
}

// Isolated ABI suites link the whole Rust archive without the C++ page sink.
// Chapter suites override this with the real destructor; accidental use fails.
extern "C" __attribute__((weak)) void xp_chapter_release_block(void*) { std::abort(); }

// Asset DEFAULT allocations use the host heap while preserving fault accounting.
extern "C" void* xp_assets_platform_alloc(size_t bytes, uint32_t caps) {
  return caps == 3 ? heap_caps_malloc(bytes, 0) : nullptr;
}
extern "C" void* xp_assets_platform_realloc(void* data, size_t bytes, uint32_t caps) {
  return caps == 3 ? heap_caps_realloc(data, bytes, 0) : nullptr;
}
extern "C" void xp_assets_platform_free(void* data) { heap_caps_free(data); }

// Suites without keyboard physical services must fail if they enter that route.
#include <xp_ui_keyboard.h>
extern "C" {
__attribute__((weak)) void xp_keyboard_launch(void*, XpAppKeyboardLaunch*) { std::abort(); }
__attribute__((weak)) void xp_keyboard_release_launch(void*) { std::abort(); }
__attribute__((weak)) XpUiKeyboardLayouts xp_keyboard_layouts() { std::abort(); }
__attribute__((weak)) XpUiKeyboardMetrics xp_keyboard_metrics(void*) { std::abort(); }
__attribute__((weak)) void xp_keyboard_snapshot(void*, XpUiKeyboardViewLayout*, uint16_t*, uint8_t*) { std::abort(); }
__attribute__((weak)) void xp_keyboard_text_lock(void*, uint8_t) { std::abort(); }
__attribute__((weak)) void xp_keyboard_draw_key(void*, const XpUiCommand*, const XpUiKeyboardPaintKey*, uint8_t,
                                                uint8_t, uint8_t) {
  std::abort();
}
__attribute__((weak)) void xp_keyboard_chrome(void*, const XpUiCommand*, XpUiRect, XpAppText) { std::abort(); }
__attribute__((weak)) void xp_keyboard_deliver(const XpAppLegacy*, void*, XpAppText, uint8_t) { std::abort(); }
}

#include <xp_dialog.h>
extern "C" {
__attribute__((weak)) void xp_dialog_launch(void*, XpDialogLaunch*) { std::abort(); }
__attribute__((weak)) void xp_dialog_release_launch(void*) { std::abort(); }
__attribute__((weak)) void xp_dialog_layout(void*, XpUiUtilityLayout*) { std::abort(); }
__attribute__((weak)) XpUiMetrics xp_dialog_metrics(void*) { std::abort(); }
__attribute__((weak)) void xp_dialog_footer(void*, XpUiRect, const XpAppText*) { std::abort(); }
__attribute__((weak)) void xp_dialog_format(uint16_t, int32_t, uint8_t, uint8_t*, size_t) { std::abort(); }
__attribute__((weak)) void xp_dialog_log(void*, uint8_t, uint32_t) { std::abort(); }
__attribute__((weak)) void xp_dialog_deliver(const XpAppLegacy*, void*, uint8_t, uint32_t) { std::abort(); }
}

// Default-heap Definition blocks have a separate host observation/failure seam.
extern "C" __attribute__((weak)) void* xp_test_definition_malloc(size_t bytes) { return std::malloc(bytes); }
extern "C" __attribute__((weak)) void xp_test_definition_free(void* data) { std::free(data); }
extern "C" __attribute__((weak)) void xp_test_definition_copied(const uint8_t*, uint8_t*, size_t) {}

// App tests that open File Transfer override this physical table. Other host
// compositions fail immediately if they accidentally route a mode activity here.
#include <xp_network_owner.h>
extern "C" __attribute__((weak))
const XpNetworkModesPhysical XP_NETWORK_MODES_PHYSICAL = {[](void*, XpUiSettingsLayout*) { std::abort(); },
                                                          [](void*) -> XpUiSettingsResources { std::abort(); },
                                                          [](void*, const XpUiCommand*, XpUiRect) { std::abort(); },
                                                          [](int32_t, uint16_t) { std::abort(); },
                                                          [](uint8_t) { std::abort(); },
                                                          [](void*, uint8_t, uint8_t) { std::abort(); }};

// Wi-Fi App fixtures provide their own table; unrelated suites must not enter it.
extern "C" __attribute__((weak)) const XpWifiPhysical XP_WIFI_PHYSICAL = {
    [](void*) -> uint8_t { std::abort(); },
    [](void*, uint8_t) { std::abort(); },
    [](void*) { std::abort(); },
    [](void*, XpUiWifiLayout*) { std::abort(); },
    [](void*, const char*, int32_t, uint8_t) -> int32_t { std::abort(); },
    [](void*, const char*, int32_t, uint8_t, uint8_t, int16_t) -> int32_t { std::abort(); },
    [](void*, int32_t) -> int32_t { std::abort(); },
    [](void*, const XpUiCommand*, XpUiRect, const XpWifiPaintData*) { std::abort(); },
    [](uint8_t*) -> int32_t { std::abort(); },
    [](XpWifiLastSsidStorage*) { std::abort(); },
    [](XpWifiLastSsidStorage*) -> XpAppText { std::abort(); },
    [](XpWifiCredentialStorage*, XpAppText, XpWifiCredentialView*) -> uint8_t { std::abort(); },
    [](XpWifiCredentialStorage*) { std::abort(); },
    [] { std::abort(); },
    [](XpAppText, XpAppText) { std::abort(); },
    [](XpAppText) { std::abort(); },
    [](XpAppText) { std::abort(); },
    []() -> uint32_t { std::abort(); },
    [](XpAppText) -> uint8_t { std::abort(); },
    [] { std::abort(); },
    [] { std::abort(); },
    [] { std::abort(); },
    []() -> int32_t { std::abort(); },
    [](uint32_t, char*) { std::abort(); },
    [](uint32_t) -> int32_t { std::abort(); },
    [](uint32_t) -> uint8_t { std::abort(); },
    [] { std::abort(); },
    [](const char*, const char*, uint8_t) { std::abort(); },
    []() -> int32_t { std::abort(); },
    [](char*) { std::abort(); },
    [] { std::abort(); },
    []() -> uint8_t { std::abort(); },
    []() -> uint8_t { std::abort(); },
    []() -> uint8_t { std::abort(); },
    [] { std::abort(); },
    [] { std::abort(); },
    [](void*) { std::abort(); },
    [](XpWifiDiagnostic, XpAppText, uint32_t) { std::abort(); },
    [](void*, uint8_t, XpAppText, XpAppText) { std::abort(); }};

#include <xp_settings_shell.h>
extern "C" {
__attribute__((weak)) void xp_settings_shell_snapshot(XpUiSettingsLayout*) { std::abort(); }
__attribute__((weak)) void xp_settings_shell_paint(const XpUiCommand*, XpUiRect, const char*, uint8_t) { std::abort(); }
__attribute__((weak)) void xp_settings_shell_theme_reload(void) { std::abort(); }
}

// Isolated core/ABI suites do not instantiate the physical renderer. Real
// renderer fixtures override these leaves; unexpected routing must fail.
#include <xp_font_registry.h>
#include <xp_sd_physical.h>
extern "C" {
__attribute__((weak)) const XpLoadedFonts* xp_native_chapter_font_owner(void*) { std::abort(); }
__attribute__((weak)) size_t heap_caps_get_largest_free_block(uint32_t) { return 200 * 1024; }
__attribute__((weak)) void xp_native_sd_handle_init(XpSdFileSlot*) { std::abort(); }
__attribute__((weak)) void xp_native_sd_handle_destroy(XpSdFileSlot*) { std::abort(); }
__attribute__((weak)) uint8_t xp_native_sd_file_open(XpSdFileSlot*, const char*, uint32_t*) { std::abort(); }
__attribute__((weak)) int32_t xp_native_sd_file_read(XpSdFileSlot*, uint8_t*, size_t) { std::abort(); }
__attribute__((weak)) uint8_t xp_native_sd_file_seek(XpSdFileSlot*, uint32_t) { std::abort(); }
__attribute__((weak)) void xp_native_sd_file_close(XpSdFileSlot*) { std::abort(); }
__attribute__((weak)) uint8_t* xp_native_sd_allocate(size_t bytes,
#ifdef XP_SD_FONT_CATALOG
                                                     uint8_t
#else
                                                     bool
#endif
) {
  return static_cast<uint8_t*>(std::malloc(bytes));
}
__attribute__((weak)) void xp_native_sd_free(uint8_t* bytes, bool) { std::free(bytes); }
__attribute__((weak)) void xp_native_sd_diagnostic(const char*, uint32_t, const uint32_t*, uint32_t, uint32_t, bool
#ifdef XP_SD_FONT_CATALOG
                                                   ,
                                                   const char*
#endif
) {
  std::abort();
}
#ifdef XP_SD_FONT_CATALOG
extern "C" __attribute__((weak)) int32_t xp_native_sd_directory_open(XpSdFileSlot*, const char*) { std::abort(); }
extern "C" __attribute__((weak)) bool xp_native_sd_directory_next(XpSdFileSlot*, char*, bool*, bool) { std::abort(); }
extern "C" __attribute__((weak)) bool xp_native_sd_path_exists(const char*) { std::abort(); }
extern "C" __attribute__((weak)) bool xp_native_sd_inspect(const char*, uint16_t*, bool*) { std::abort(); }
#endif
__attribute__((weak)) size_t heap_caps_get_free_size(uint32_t) { return 200 * 1024; }
__attribute__((weak)) int64_t esp_timer_get_time() { return 0; }
__attribute__((weak)) void* fiFontMalloc(size_t n) { return std::malloc(n); }
__attribute__((weak)) void fiFontFree(void* p) { std::free(p); }
__attribute__((weak)) void xp_native_font_cache_diagnostic(const void*, uint32_t, uint32_t, uint32_t, uint32_t,
                                                           uint32_t) {
  std::abort();
}
__attribute__((weak)) uint8_t xp_native_font_compat_resolve(void*, int32_t, uint8_t) { std::abort(); }
__attribute__((weak)) int32_t xp_native_font_compat_prewarm(void*, int32_t, uint8_t, const char*, uint8_t, uint8_t,
                                                            uint8_t, uint8_t) {
  std::abort();
}
__attribute__((weak)) void xp_native_font_compat_clear(void*, int32_t, uint8_t, uint8_t) { std::abort(); }
__attribute__((weak)) void xp_native_font_compat_reset(void*, int32_t) { std::abort(); }
__attribute__((weak)) const uint8_t* xp_native_sd_font_bitmap(const void*, const void*) { std::abort(); }
__attribute__((weak)) void xp_native_sd_font_view(void*, int32_t, XpNativeFamily*, XpGlyphSource*) { std::abort(); }
__attribute__((weak)) void xp_native_sd_font_resident_slice(void*, int32_t, uint8_t, const uint8_t*, size_t, uint8_t) {
  std::abort();
}
__attribute__((weak)) uint8_t xp_native_sd_font_load(void*, int32_t) { std::abort(); }
__attribute__((weak)) void xp_native_font_diagnostic(void*, uint32_t, uint32_t) { std::abort(); }
__attribute__((weak)) void* xp_native_bidi_acquire() { std::abort(); }
__attribute__((weak)) void xp_native_bidi_release() { std::abort(); }
}

#include <xp_page_paint.h>
extern "C" {
__attribute__((weak)) void xp_native_page_test_glyph(const void*, const XpGlyphPlacement*, int32_t, int32_t, uint8_t) {}
__attribute__((weak)) uint8_t xp_native_render_target(XpPageTarget*, XpUiPaint*) { std::abort(); }
__attribute__((weak)) size_t xp_native_page_image_file(XpPageImageFile*, XpPageImageFileOp, const uint8_t*, uint8_t*,
                                                       size_t) {
  std::abort();
}
__attribute__((weak)) uint8_t xp_native_page_image_decode(void*, const XpPageImage*, const uint8_t*) { std::abort(); }
__attribute__((weak)) uint8_t xp_native_page_image_extract(const XpPageImage*, uint8_t) { std::abort(); }
__attribute__((weak)) void xp_native_page_image_log(void*, const XpPageImage*, const uint8_t*, uint32_t, int32_t,
                                                    int32_t) {
  std::abort();
}
}
