#include "Runtime.h"

#include <cstdarg>

#include "BleRust.h"

namespace bleturner {
namespace {
Heap noHeap() { return {0, 0}; }
bool noRelease() { return true; }
bool noDeliver(Action) { return false; }
bool noYield() { return true; }
bool noTransfer() { return false; }
void noRestart() {}
void noHold(bool) {}
void noLog(bool, const char*, va_list) {}
constexpr Host kNoHost{noHeap, noRelease, noDeliver, noYield, noTransfer, noRestart, noHold, noLog, nullptr};
Config noConfig;
const Host* host = &kNoHost;

void say(const bool error, const char* format, ...) {
  va_list args;
  va_start(args, format);
  host->log(error, format, args);
  va_end(args);
}

void diagnostic(uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  switch (static_cast<rust::Diagnostic>(event)) {
    case rust::Diagnostic::SkippedTransfer:
    case rust::Diagnostic::SkippedHeap:
      say(true, "HID begin skipped (%s): free=%zu largest=%zu required_free=%zu required_largest=%zu\n",
          event == static_cast<uint32_t>(rust::Diagnostic::SkippedTransfer) ? "filetransfer-active"
                                                                            : "insufficient-internal-heap",
          static_cast<size_t>(a), static_cast<size_t>(b), activeProfile().startFreeBytes,
          activeProfile().minimumLargest);
      break;
    case rust::Diagnostic::BeginHeap:
#ifdef BLE_PAGE_TURNER_PROBE
      say(false, "HID begin heap free=%zu largest=%zu\n", static_cast<size_t>(a), static_cast<size_t>(b));
#endif
      break;
    case rust::Diagnostic::RolledBack:
      say(true, "HID begin rolled back (post-init-headroom): free=%zu largest=%zu required_largest=%zu\n",
          static_cast<size_t>(a), static_cast<size_t>(b), activeProfile().minimumLargest);
      break;
    case rust::Diagnostic::KeptHeap:
#ifdef BLE_PAGE_TURNER_PROBE
      say(false, "HID begin kept free=%zu largest=%zu\n", static_cast<size_t>(a), static_cast<size_t>(b));
#endif
      break;
    case rust::Diagnostic::ReconnectNotArmed:
      say(false, "Bonded reader reconnect was not armed\n");
      break;
    case rust::Diagnostic::Raw:
      say(false, "raw %u:%u=%02X %s -> %s\n", (a >> 16) & 255, (a >> 8) & 255, a & 255, b ? "down" : "up",
          actionName(static_cast<Action>(c)));
      break;
    case rust::Diagnostic::Hold:
      say(false, "raw hold -> %s\n", actionName(static_cast<Action>(a)));
      break;
    case rust::Diagnostic::Key:
      say(false, "key 0x%02X mods 0x%02X %s -> %s\n", a, b, c ? "down" : "up",
          d == static_cast<uint32_t>(Action::PrevPage)   ? "previous"
          : d == static_cast<uint32_t>(Action::NextPage) ? "next"
                                                         : "none");
      break;
    case rust::Diagnostic::Retry:
      say(false, "Retrying reader BLE start after memory refusal (%u)\n", a);
      break;
    case rust::Diagnostic::Requested:
      say(false, "Reader BLE start requested\n");
      break;
    case rust::Diagnostic::Deferred:
      say(true, "Reader BLE start deferred: insufficient memory or unavailable radio\n");
      break;
    case rust::Diagnostic::Idle:
      say(false, "Radio idle for %u ms with nothing connected; stopping\n", a);
      break;
    case rust::Diagnostic::Restart:
      say(false, "Heap fragmented for radio: free=%u largest=%u; silent restart to reader\n", a, b);
      break;
    case rust::Diagnostic::BuildStarved:
      say(false, "Section build starved of heap; stopping the radio until the page is shown free=%u largest=%u\n", a,
          b);
      break;
    case rust::Diagnostic::BuildStopFailed:
      say(true, "Radio did not stop for the section build\n");
      break;
  }
}

void heapMap() {
  static uint8_t mapsLeft = 2;
  if (mapsLeft > 0 && host->heapMap != nullptr) {
    --mapsLeft;
    host->heapMap("ble-skipped");
  }
}

const rust::Services services{[] { return host->heap(); },
                              [] { return host->releaseCaches(); },
                              [](uint8_t action) { return host->deliver(static_cast<Action>(action)); },
                              [] { return host->yieldForRadio(); },
                              [] { return host->fileTransferActive(); },
                              [] { host->restartIntoBook(); },
                              [](bool hold) { host->holdFullSpeed(hold); },
                              diagnostic,
                              heapMap,
                              port::compiledIn,
                              port::begin,
                              port::end,
                              port::running,
                              port::stopping,
                              port::connected,
                              port::connecting,
                              port::connectionActivity,
                              port::scanning,
                              port::poll,
                              [](port::RawEdge* out) { return port::popRaw(*out); },
                              [](port::KeyPress* out) { return port::popKey(*out); },
                              port::armBondedReconnect,
                              port::linked,
                              port::scan,
                              port::connect,
                              port::disconnect,
                              port::forget,
                              port::spawnStart,
                              port::nowMs,
                              port::sleepMs,
                              [] { return &port::restartMemo().magic; },
                              defaultTableFor,
                              &activeProfile()};
struct Initialize {
  Initialize() { rust::xp_ble_init(&services, &noConfig); }
} initialize;
}  // namespace

namespace detail {
void startTask() { rust::xp_ble_start_task(); }
bool startAsync() { return rust::xp_ble_start_async(); }
bool suspend(uint32_t timeout) { return rust::xp_ble_suspend(timeout); }
bool stopForIdle() { return rust::xp_ble_stop_idle(); }
bool heapRestartWanted() { return rust::xp_ble_restart_wanted(); }
#ifdef BLETURNER_TESTING
void resetForTests() {
  host = &kNoHost;
  noConfig = Config{};
  rust::xp_ble_init(&services, &noConfig);
  rust::xp_ble_reset();
}
#endif
}  // namespace detail

void begin(const Host& h, Config& c) {
  host = &h;
  rust::xp_ble_init(&services, &c);
}
void requestConnect() { rust::xp_ble_request_connect(); }
bool tick(const Scene& scene) { return rust::xp_ble_tick(&scene); }
bool beforeScreenChange(uint32_t timeout, bool nextIsReader) {
  return rust::xp_ble_before_screen(timeout, nextIsReader);
}
bool stopNow(uint32_t timeout) { return rust::xp_ble_stop_now(timeout); }
bool beforeSleep(uint32_t timeout) { return rust::xp_ble_stop_now(timeout); }
void settleStart() { rust::xp_ble_settle_start(); }
BuildRelease beforeChapterBuild() { return static_cast<BuildRelease>(rust::xp_ble_before_build()); }
void afterPaint() { rust::xp_ble_after_paint(); }
bool holdsHeap() { return rust::xp_ble_holds_heap(); }
Status status() {
  Status result{};
  rust::xp_ble_status(&result);
  return result;
}
Why why() { return static_cast<Why>(rust::xp_ble_why()); }
LinkNote linkNote() { return static_cast<LinkNote>(rust::xp_ble_note()); }
void acknowledgeLinkNote() { rust::xp_ble_acknowledge(); }
bool switchOn() { return rust::xp_ble_switch_on(); }
bool switchOff() { return rust::xp_ble_switch_off(); }
bool service() { return rust::xp_ble_service(); }
void scan(uint32_t duration) { port::scan(duration); }
bool pair(const char* addr, const char* name) { return rust::xp_ble_pair(addr, name); }
void disconnect() { port::disconnect(); }
bool forget(const char* addr) { return rust::xp_ble_forget(addr); }
uint8_t bondCount() { return port::bondCount(); }
Peer bond(uint8_t index) { return port::bond(index); }
uint8_t foundCount() { return port::foundCount(); }
Peer found(uint8_t index) { return port::found(index); }
Peer linked() { return port::linked(); }
bool takeConnectFailure(char* out, size_t len) { return port::takeConnectFailure(out, len); }
bool pollEvent(Event& event) { return rust::xp_ble_poll_event(&event); }
#ifdef BLE_PAGE_TURNER_PROBE
void injectFrame(const uint8_t* frame, size_t len) {
  rust::xp_ble_probe_table();
  if (len > 0) port::inject(frame, len);
}
unsigned rawOverflows() { return port::rawOverflows(); }
#endif
#ifdef BLE_PAGE_TURNER_STACK_PROBE
uint32_t startStackLeft() { return port::startStackLeft(); }
#endif
}  // namespace bleturner

// Platform synchronization leaf for Rust targets without native atomics (ESP32-C3).
extern "C" {
uint8_t xp_ble_atomic_load8(const uint8_t* value, int order) { return __atomic_load_n(value, order); }
void xp_ble_atomic_store8(uint8_t* value, uint8_t desired, int order) { __atomic_store_n(value, desired, order); }
uint8_t xp_ble_atomic_swap8(uint8_t* value, uint8_t desired, int order) {
  return __atomic_exchange_n(value, desired, order);
}
bool xp_ble_atomic_cas8(uint8_t* value, uint8_t* expected, uint8_t desired, int success, int failure) {
  return __atomic_compare_exchange_n(value, expected, desired, false, success, failure);
}
}
