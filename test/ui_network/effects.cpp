#include <algorithm>
#include <cassert>
#include <chrono>
#include <future>
#include <mutex>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

static uint32_t nowMs;
static std::vector<std::string> transcript;
static void record(const std::string& s) { transcript.push_back(s); }
static void log(const char* level, const char* tag, const char* fmt, ...) {
  char text[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  record(std::string(level) + ":" + tag + ":" + text);
}
#define LOG_DBG(tag, ...) log("DBG", tag, __VA_ARGS__)
#define LOG_INF(tag, ...) log("INF", tag, __VA_ARGS__)
#define LOG_ERR(tag, ...) log("ERR", tag, __VA_ARGS__)
uint32_t millis() { return nowMs; }
void delay(uint32_t ms) {
  record("delay:" + std::to_string(ms));
  nowMs += ms;
}
void yield() { record("yield"); }
void silentRestart() { record("restart"); }
void restartToHomeAfterStorageHandoff() { record("restart-storage"); }
void resetTaskWatchdogIfSubscribed() { record("watchdog"); }
size_t strlcpy(char* dst, const char* src, size_t size) {
  const auto n = strlen(src);
  if (size) {
    const auto used = std::min(n, size - 1);
    memcpy(dst, src, used);
    dst[used] = 0;
  }
  return n;
}
struct MappedInputManager {
  enum class Button { Back, Confirm, NavNext, NavPrevious, Left, Right, Up, Down, Power };
  enum class SwipeDir { None, Up, Down, Left, Right };
  uint32_t pressed = 0, released = 0, held = 0;
  bool home = false;
  int updates = 0;
  SwipeDir swipe = SwipeDir::None;
  bool wasPressed(Button b) const { return pressed & (1u << unsigned(b)); }
  bool wasReleased(Button b) const { return released & (1u << unsigned(b)); }
  bool isPressed(Button b) const { return held & (1u << unsigned(b)); }
  bool wasHomeGesture() const { return home; }
  SwipeDir wasSwipe() const { return swipe; }
  void update(bool force) {
    record("input-update:" + std::to_string(force));
    ++updates;
  }
} input;
static std::recursive_mutex panelMutex;
struct RenderLock {
  template <class T>
  explicit RenderLock(T&) {
    panelMutex.lock();
    record("lock");
  }
  ~RenderLock() { record("unlock"); panelMutex.unlock(); }
};
struct FontCacheManager {
  void releaseSdFontCaches() { record("font-cache-release"); }
} cache;
struct GfxRenderer {
  FontCacheManager* getFontCacheManager() { return &cache; }
} renderer;
struct {
  int getFreeHeap() { return 123456; }
} ESP;
struct Route {
  bool routed = false;
  explicit operator bool() const { return false; }
};
struct WifiResult {
  bool connected;
  std::string ssid, ip;
};
struct ActivityResult {
  bool isCancelled = false;
  WifiResult data{};
};
struct Activity {
  MappedInputManager& mappedInput = input;
  GfxRenderer& renderer = ::renderer;
  struct App {
    bool invalidated() { return false; }
    void clearTapFlash() { record("clear-flash"); }
    template <class T>
    void setScreen(T, void*) {}
  } app;
  virtual ~Activity() = default;
  void onEnter() { record("enter"); }
  void onExit() { record("exit"); }
  void resetUi() { record("reset-ui"); }
  void requestUpdate() { record("paint"); }
  void requestUpdateAndWait() { record("paint-wait"); }
  void finish() { record("finish"); }
  void onGoHome() { record("home"); }
  void setResult(ActivityResult r) {
    record("result:" + std::to_string(r.isCancelled) + ":" + r.data.ssid + ":" + r.data.ip);
  }
  Route routeTouch(MappedInputManager&, bool = false) { return {}; }
};
enum class UsbDriveState { Unsupported, WaitingForHost, Connected, Ejected, Disconnected, IoError };
struct {
  UsbDriveState state = UsbDriveState::WaitingForHost;
  bool begin = true, suspended = false, disconnect = true;
  bool beginUsbDrive() {
    record("usb-begin");
    return begin;
  }
  void endUsbDrive() { record("usb-end"); }
  UsbDriveState usbDriveState() {
    record("usb-state");
    return state;
  }
  bool usbDriveHostSuspended() {
    record("usb-suspended");
    return suspended;
  }
  bool disconnectUsbDriveHost() {
    record("usb-disconnect");
    return disconnect;
  }
} Storage;
struct UsbDriveActivity : Activity {
  using State = UsbDriveState;
  State state = State::Unsupported;
  bool preparing = true, startFailed = false, restartRequested = false, forcedDisconnectRequested = false,
       hostSuspendPending = false;
  uint32_t hostWaitStartedAt = 0, startFailureStartedAt = 0, forcedDisconnectRequestedAt = 0, hostSuspendStartedAt = 0;
  static constexpr uint32_t HOST_WAIT_TIMEOUT_MS = 300000, START_FAILURE_TIMEOUT_MS = 30000,
                            FORCED_DISCONNECT_TIMEOUT_MS = 1000, HOST_SUSPEND_TIMEOUT_MS = 2000;
  bool preventAutoSleep() { return state == State::Connected || (!startFailed && state == State::IoError); }
  static void driveScreen() {}
  void onEnter();
  void onExit();
  void loop();
  void restartToHome();
};
using wl_status_t = int;
constexpr int WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_NO_SSID_AVAIL = 1, WIFI_SCAN_RUNNING = -1,
              WIFI_SCAN_FAILED = -2, WIFI_STA = 1, WIFI_AP = 2, WIFI_MODE_NULL = 0, WIFI_AUTH_OPEN = 0,
              WIFI_ALL_CHANNEL_SCAN = 1, WIFI_CONNECT_AP_BY_SIGNAL = 1;
struct IPAddress {
  uint8_t operator[](size_t n) const { return n == 3 ? 7 : 10; }
};
struct ScanRow {
  std::string ssid;
  int rssi;
  int auth;
};
struct {
  int connected = 0, modeValue = 0, scan = -1, signal = -60;
  bool ap = true;
  std::vector<ScanRow> rows;
  void persistent(bool b) { record("wifi-persistent:" + std::to_string(b)); }
  void mode(int m) {
    record("wifi-mode:" + std::to_string(m));
    modeValue = m;
  }
  int getMode() {
    record("wifi-get-mode");
    return modeValue;
  }
  void disconnect(bool a = false, bool b = false) {
    record("wifi-disconnect:" + std::to_string(a) + ":" + std::to_string(b));
  }
  void softAPdisconnect(bool b) { record("ap-disconnect:" + std::to_string(b)); }
  void scanDelete() { record("scan-delete"); }
  void scanNetworks(bool b) { record("scan-start:" + std::to_string(b)); }
  int scanComplete() {
    record("scan-complete");
    return scan;
  }
  std::string SSID(size_t i) { return rows.at(i).ssid; }
  int RSSI(size_t i) { return rows.at(i).rssi; }
  int RSSI() { return signal; }
  int encryptionType(size_t i) { return rows.at(i).auth; }
  int status() {
    record("wifi-status");
    return connected;
  }
  IPAddress localIP() {
    record("local-ip");
    return {};
  }
  IPAddress softAPIP() {
    record("ap-ip");
    return {};
  }
  void setScanMethod(int m) { record("scan-method:" + std::to_string(m)); }
  void setSortMethod(int m) { record("sort-method:" + std::to_string(m)); }
  void setHostname(const char* s) { record(std::string("hostname:") + s); }
  void begin(const char* s, const char* p = nullptr) {
    record(std::string("wifi-begin:") + s + ":" + (p ? p : "<null>"));
  }
  bool softAP(const char* s, const char* p, int ch, bool hidden, int max) {
    record(std::string("ap-begin:") + s + ":" + (p ? p : "<null>") + ":" + std::to_string(ch) + ":" +
           std::to_string(hidden) + ":" + std::to_string(max));
    return ap;
  }
} WiFi;
using esp_err_t = int;
constexpr int ESP_MAC_WIFI_STA = 0, ESP_OK = 0;
int esp_read_mac(uint8_t* mac, int) {
  record("read-mac");
  for (int i = 0; i < 6; ++i) mac[i] = i;
  return 0;
}
struct WifiCredential {
  std::string ssid, password;
};
struct {
  std::vector<WifiCredential> credentials;
  bool hasSavedCredential(const std::string& s) { return bool(findCredential(s)); }
  std::optional<WifiCredential> findCredential(const std::string& s) {
    for (const auto& c : credentials)
      if (c.ssid == s) return c;
    return {};
  }
  void setLastConnectedSsid(const std::string& s) { record("persist-last:" + s); }
  void addCredential(const std::string& s, const std::string& p) { record("persist-add:" + s + ":" + p); }
  void removeCredential(const std::string& s) { record("persist-remove:" + s); }
} WIFI_STORE;
struct {
  bool available = true, sync = true;
  bool isAvailable() {
    record("clock-available");
    return available;
  }
  bool syncFromNTP() {
    record("clock-sync");
    return sync;
  }
} halClock;
struct {
  bool clockHasBeenSynced = false;
  void saveToFile() { record("settings-save"); }
} SETTINGS;
namespace trustedtime {
void startSync() { record("trusted-time"); }
}  // namespace trustedtime
namespace pluginevents {
void drain(GfxRenderer*) { record("plugin-drain"); }
void refreshSubscriptions() { record("plugin-refresh"); }
}  // namespace pluginevents
#define tr(x) #x
struct ButtonNavigator {
  template <class F>
  void onNext(F f) {
    if (input.wasPressed(MappedInputManager::Button::NavNext)) f();
  }
  template <class F>
  void onPrevious(F f) {
    if (input.wasPressed(MappedInputManager::Button::NavPrevious)) f();
  }
  static int nextIndex(size_t n, size_t count) { return count ? (n + 1) % count : 0; }
  static int previousIndex(size_t n, size_t count) { return count ? (n + count - 1) % count : 0; }
};
struct WifiNetworkInfo {
  std::string ssid;
  int32_t rssi;
  bool isEncrypted, hasSavedPassword, isHiddenPlaceholder = false;
};
enum class WifiSelectionState {
  AUTO_CONNECTING,
  SCANNING,
  NETWORK_LIST,
  HIDDEN_SSID_ENTRY,
  PASSWORD_ENTRY,
  CONNECTING,
  CONNECTED,
  SAVE_PROMPT,
  CONNECTION_FAILED,
  FORGET_PROMPT
};
struct WifiSelectionActivity : Activity {
  WifiSelectionState state = WifiSelectionState::SCANNING;
  size_t selectedNetworkIndex = 0, realNetworkCount = 0;
  std::vector<WifiNetworkInfo> networks;
  std::vector<std::string> networkStatuses;
  std::vector<int> networkRowItems;
  std::string selectedSSID, connectedIP, connectionError, enteredPassword;
  bool selectedRequiresPassword = false, usedSavedPassword = false, autoConnecting = false,
       manualNetworkListRequested = false;
  std::vector<std::string> autoAttemptedSsids;
  int savePromptSelection = 0, forgetPromptSelection = 0;
  uint32_t connectionStartTime = 0;
  static constexpr uint32_t CONNECTION_TIMEOUT_MS = 15000, AUTO_CONNECTION_TIMEOUT_MS = 7000;
  struct {
    int selected = 0, visibleRows = 4;
    void reset() { record("viewport-reset"); }
    bool scrollBy(int, int) {
      record("scroll");
      return true;
    }
    void follow(int) { record("follow"); }
  } listNav;
  ButtonNavigator buttonNavigator;
  void rebuildNetworkRowItems() { record("rebuild-rows"); }
  void promptPasswordEntry() {
    state = WifiSelectionState::PASSWORD_ENTRY;
    record("keyboard-password");
  }
  void promptHiddenSsid() { record("keyboard-ssid"); }
  void startWifiScan(bool = false);
  void processWifiScanResults();
  void appendHiddenNetworkEntry();
  void selectNetwork(int);
  bool hasAttemptedAutoSsid(const std::string&) const;
  bool tryAutoConnectCredential(const WifiCredential&);
  bool tryNextSavedNetworkFromScan();
  void handleAutoConnectFailure();
  void showNetworkListFromAutoConnect();
  void attemptConnection();
  void checkConnectionStatus();
  void loop();
  void onComplete(bool);
};
struct {
  void end() { record("mdns-end"); }
  bool begin(const char* name) {
    record(std::string("mdns-begin:") + name);
    return true;
  }
} MDNS;
constexpr const char* HOSTNAME = "crosspoint";
struct CrossPointWebServer {
  struct Status {
    bool inProgress = false;
    size_t received = 0, total = 0;
    std::string filename, lastCompleteName;
    uint32_t lastCompleteAt = 0;
  };
  inline static bool startOk = true;
  static Status nextStatus;
  bool running = false;
  int clients = 0;
  std::function<bool()> exitCallback;
  CrossPointWebServer() { record("server-alloc"); }
  ~CrossPointWebServer() { record("server-delete"); }
  void begin() {
    record("server-begin");
    running = startOk;
  }
  bool isRunning() { return running; }
  void handleClient() {
    record("client");
    ++clients;
  }
  Status getWsUploadStatus() {
    record("upload-status");
    return nextStatus;
  }
  void setUploadCancelCheck(std::function<bool()> cb) {
    record("exit-callback");
    exitCallback = std::move(cb);
  }
};
CrossPointWebServer::Status CrossPointWebServer::nextStatus{};
struct {
  bool rawInputActive() { return false; }
} gpio;
enum class CalibreConnectState { WIFI_SELECTION, SERVER_STARTING, SERVER_RUNNING, ERROR };
struct CalibreConnectActivity : Activity {
  CalibreConnectState state = CalibreConnectState::WIFI_SELECTION;
  std::unique_ptr<CrossPointWebServer> webServer;
  uint32_t lastHandleClientTime = 0, lastCompleteAt = 0, lastProcessedCompleteAt = 0;
  size_t lastProgressReceived = 0, lastProgressTotal = 0;
  std::string currentUploadName, lastCompleteName;
  bool exitRequested = false;
  void onExit();
  void onWifiSelectionComplete(bool);
  void startWebServer();
  void loop();
};
constexpr const char* AP_SSID = "CrossPoint-Reader";
constexpr const char* AP_PASSWORD = nullptr;
constexpr const char* AP_HOSTNAME = "crosspoint";
constexpr int AP_CHANNEL = 1, AP_MAX_CONNECTIONS = 4, DNS_PORT = 53;
enum class DNSReplyCode { NoError };
struct DNSServer {
  DNSServer() { record("dns-alloc"); }
  ~DNSServer() { record("dns-delete"); }
  void stop() { record("dns-stop"); }
  void setErrorReplyCode(DNSReplyCode) { record("dns-reply-code"); }
  void start(int, const char*, IPAddress) { record("dns-start"); }
  void processNextRequest() { record("dns-next"); }
};
DNSServer* dnsServer = nullptr;
// PRODUCTION_HELPERS


enum class WebServerActivityState { MODE_SELECTION, WIFI_SELECTION, AP_STARTING, SERVER_RUNNING, SHUTTING_DOWN };
struct CrossPointWebServerActivity : Activity {
  WebServerActivityState state = WebServerActivityState::MODE_SELECTION;
  bool isApMode = false, leaveRequested = false;
  std::unique_ptr<CrossPointWebServer> webServer;
  std::string connectedIP, connectedSSID;
  uint32_t lastHandleClientTime = 0, firstDisconnectAt = 0;
  int consecutiveDisconnects = 0, lastWifiBars = 0;
  static constexpr uint32_t WIFI_ABANDON_MS = 300000;
  void onExit();
  void startAccessPoint();
  void startWebServer();
  void loop();
};

// PRODUCTION_METHODS

static void start(const std::string& name) {
  input = {};
  nowMs = 100;
  WiFi = {};
  Storage = {};
  WIFI_STORE = {};
  SETTINGS = {};
  halClock = {};
  CrossPointWebServer::startOk = true;
  CrossPointWebServer::nextStatus = {};
  transcript.clear();
  record("CASE:" + name);
}
static void flush() {
  for (const auto& s : transcript) std::puts(s.c_str());
}
int main() {
  start("rssi-hysteresis");
  for (int current=-1;current<=5;++current) for(int rssi=-100;rssi<=0;++rssi)
    record("rssi:"+std::to_string(current)+":"+std::to_string(rssi)+":"+std::to_string(barsForRssi(rssi,current)));
  flush();
  for (bool ok : {false, true}) {
    start("usb-entry-" + std::to_string(ok));
    {
      UsbDriveActivity a;
      Storage.begin = ok;
      a.onEnter();
      a.onExit();
    }
    flush();
  }
  for (int state = 0; state < 6; ++state)
    for (uint32_t age : {0u, 999u, 1000u, 1999u, 2000u, 29999u, 30000u, 299999u, 300000u}) {
      start("usb-" + std::to_string(state) + "-" + std::to_string(age));
      {
        UsbDriveActivity a;
        Storage.state = UsbDriveState(state);
        Storage.suspended = true;
        a.state = Storage.state;
        a.hostWaitStartedAt = 100;
        a.hostSuspendPending = true;
        a.hostSuspendStartedAt = 100;
        a.forcedDisconnectRequested = true;
        a.forcedDisconnectRequestedAt = 100;
        nowMs += age;
        a.loop();
      }
      flush();
    }
  for (int button : {-1, 0, 8}) {
    start("usb-input-" + std::to_string(button));
    {
      UsbDriveActivity a;
      Storage.state = UsbDriveState::WaitingForHost;
      if (button >= 0) input.pressed = 1u << button;
      a.loop();
    }
    flush();
  }
  for (int scenario = 0; scenario < 4; ++scenario)
    for (uint32_t age : {0u, 999u, 1000u, 1999u, 2000u, 29999u, 30000u, 299999u, 300000u}) {
      start("usb-wrap-" + std::to_string(scenario) + "-" + std::to_string(age));
      UsbDriveActivity a;
      const uint32_t origin = 0xfffffff0u;
      a.hostWaitStartedAt = origin;
      a.startFailureStartedAt = origin;
      a.forcedDisconnectRequestedAt = origin;
      a.hostSuspendStartedAt = origin;
      a.state = scenario == 0   ? UsbDriveState::WaitingForHost
                : scenario == 3 ? UsbDriveState::Connected
                                : UsbDriveState::IoError;
      Storage.state = a.state;
      Storage.suspended = true;
      a.startFailed = scenario == 1;
      a.forcedDisconnectRequested = true;
      a.hostSuspendPending = true;
      nowMs = origin + age;
      a.loop();
      a.onExit();
      flush();
    }
  for (bool disconnectOk : {false, true}) {
    start("usb-io-disconnect-" + std::to_string(disconnectOk));
    UsbDriveActivity a;
    Storage.state = UsbDriveState::IoError;
    Storage.disconnect = disconnectOk;
    a.loop();
    nowMs += 1000;
    a.loop();
    a.loop();
    a.onExit();
    flush();
  }
  for (bool failed : {false, true}) {
    start("usb-home-" + std::to_string(failed));
    UsbDriveActivity a;
    Storage.state = UsbDriveState::WaitingForHost;
    a.startFailed = failed;
    input.home = true;
    a.loop();
    a.onExit();
    flush();
  }
  for (int phase=0;phase<6;++phase) for (bool failed : {false,true}) {
    start("usb-sleep-"+std::to_string(phase)+"-"+std::to_string(failed));
    UsbDriveActivity a; a.state=static_cast<UsbDriveState>(phase);a.startFailed=failed;
    record("prevent-sleep:"+std::to_string(a.preventAutoSleep()));flush();
  }
  {
    start("usb-panel-busy");UsbDriveActivity a;Storage.state=UsbDriveState::WaitingForHost;
    std::lock_guard<std::recursive_mutex> panelBusy(panelMutex);
    for (bool press : {true,false}) {
      input.pressed=press?1:0;input.released=press?0:1;
      auto handled=std::async(std::launch::async,[&a]{a.loop();});
      assert(handled.wait_for(std::chrono::seconds(2))==std::future_status::ready);
      handled.get();record(press?"busy-press-handled":"busy-release-handled");
    }
    flush();
  }
  for (bool automatic : {false, true}) {
    start("wifi-scan-" + std::to_string(automatic));
    {
      WifiSelectionActivity a;
      a.startWifiScan(automatic);
      WiFi.scan = 4;
      WiFi.rows = {{"zeta", -80, 1}, {"alpha", -70, 0}, {"zeta", -40, 0}, {"", -20, 1}};
      WIFI_STORE.credentials = {{"alpha", "secret"}};
      a.processWifiScanResults();
      for (const auto& n : a.networks)
        record("row:" + n.ssid + ":" + std::to_string(n.rssi) + ":" + std::to_string(n.isEncrypted) + ":" +
               std::to_string(n.hasSavedPassword) + ":" + std::to_string(n.isHiddenPlaceholder));
    }
    flush();
  }
  for (int result : {-2, -1, 0}) {
    start("wifi-scan-result-" + std::to_string(result));
    WifiSelectionActivity a;
    WiFi.scan = result;
    a.processWifiScanResults();
    flush();
  }
  for (bool saved : {false, true})
    for (bool automatic : {false, true})
      for (int status : {0, 1, 3, 4})
        for (uint32_t age : {7000u, 7001u, 15000u, 15001u}) {
          start("wifi-connect-" + std::to_string(saved) + "-" + std::to_string(automatic) + "-" +
                std::to_string(status) + "-" + std::to_string(age));
          WifiSelectionActivity a;
          a.selectedSSID = "test";
          a.enteredPassword = "secret";
          a.selectedRequiresPassword = true;
          a.usedSavedPassword = saved;
          a.autoConnecting = automatic;
          a.attemptConnection();
          nowMs = a.connectionStartTime + age;
          WiFi.connected = status;
          a.checkConnectionStatus();
          record("state:" + std::to_string(int(a.state)));
          flush();
        }
  for (int state : {7, 9})
    for (int selection : {0, 1})
      for (int button : {0, 1, 4, 5, 6, 7}) {
        start("wifi-prompt-" + std::to_string(state) + "-" + std::to_string(selection) + "-" + std::to_string(button));
        WifiSelectionActivity a;
        a.state = WifiSelectionState(state);
        a.savePromptSelection = selection;
        a.forgetPromptSelection = selection;
        a.selectedSSID = "test";
        a.enteredPassword = "secret";
        input.pressed = 1u << button;
        a.loop();
        record("selection:" + std::to_string(a.savePromptSelection) + ":" + std::to_string(a.forgetPromptSelection));
        flush();
      }
  for (bool ok : {false, true}) {
    start("calibre-start-" + std::to_string(ok));
    {
      CalibreConnectActivity a;
      CrossPointWebServer::startOk = ok;
      a.startWebServer();
      a.onExit();
    }
    flush();
  }
  for (bool back : {false, true}) {
    start("calibre-loop-" + std::to_string(back));
    {
      CalibreConnectActivity a;
      a.startWebServer();
      input.pressed = back ? 1 : 0;
      CrossPointWebServer::nextStatus.inProgress = true;
      CrossPointWebServer::nextStatus.received = 10;
      CrossPointWebServer::nextStatus.total = 100;
      CrossPointWebServer::nextStatus.filename = "book.epub";
      a.loop();
      CrossPointWebServer::nextStatus = {};
      CrossPointWebServer::nextStatus.lastCompleteAt = 100;
      CrossPointWebServer::nextStatus.lastCompleteName = "book.epub";
      a.loop();
      nowMs = 6100;
      a.loop();
      a.loop();
    }
    flush();
  }
  for (bool ok : {false, true}) {
    start("web-ap-" + std::to_string(ok));
    {
      CrossPointWebServerActivity a;
      a.isApMode = true;
      WiFi.ap = ok;
      a.startAccessPoint();
      a.onExit();
    }
    flush();
  }
  for (bool ok : {false, true}) {
    start("web-server-" + std::to_string(ok));
    {
      CrossPointWebServerActivity a;
      CrossPointWebServer::startOk = ok;
      a.startWebServer();
      a.onExit();
    }
    flush();
  }
  for (bool leave : {false, true}) {
    start("web-loop-" + std::to_string(leave));
    {
      CrossPointWebServerActivity a;
      a.startWebServer();
      a.leaveRequested = leave;
      a.loop();
      a.onExit();
    }
    flush();
  }
}
