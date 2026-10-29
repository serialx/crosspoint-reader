#include "CrossPointWebServer.h"

#include <ArduinoJson.h>
#include <BoardConfig.h>
#include <BookKey.h>
#include <Crypto.h>
#include <DeviceSecret.h>
#include <FsHelpers.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>
#include <ResumableFetch.h>
#include <SecureHttpClient.h>
#include <Util.h>
#include <WiFi.h>
#include <WolfsslCrypto.h>
#include <base64.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <xp_web.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>

#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "FontInstaller.h"
#include "OpdsServerStore.h"
#include "ProtectedPaths.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "WebDAVHandler.h"
#include "WifiCredentialStore.h"
#include "WifiPowerSaveGuard.h"
#include "html/FilesPageHtml.generated.h"
#include "html/FontsPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/RunnerPageHtml.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "html/assets/crosspoint_iconSvg.generated.h"
#include "html/assets/iconsSvg.generated.h"
#include "html/assets/webCss.generated.h"
#include "html/js/jszip_minJs.generated.h"
#include "util/BookCacheUtils.h"
#include "util/DictionaryRegistry.h"
#include "util/FirmwareIdentity.h"
#include "util/PluginHttp.h"
#include "util/PluginLocations.h"
#include "util/TaskWatchdog.h"

static void sendStaticContent(WebServer* server, const char* data, size_t len, const char* etag,
                              const char* contentType);

namespace {
// Arduino's WebServer retains parsed request arguments until the next request.
// For JSON POSTs, that includes the complete "plain" body. Expose a narrowly
// scoped release operation so large request bodies do not remain resident
// between requests, and so outbound TLS can reuse that memory immediately.
class CrossPointHttpServer final : public WebServer {
 public:
  explicit CrossPointHttpServer(uint16_t port) : WebServer(port) {}

  void releaseRequestArguments() {
    if (_currentArgs) {
      delete[] _currentArgs;
      _currentArgs = nullptr;
    }
    _currentArgCount = 0;

    if (_postArgs) {
      delete[] _postArgs;
      _postArgs = nullptr;
    }
    _postArgsLen = 0;
  }
};

void releaseRequestArguments(WebServer* server) {
  static_cast<CrossPointHttpServer*>(server)->releaseRequestArguments();
}

constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;

// Static pointer for WebSocket callback (WebSocketsServer requires C-style callback)
CrossPointWebServer* wsInstance = nullptr;

// WebSocket upload state
HalFile wsUploadFile;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
unsigned long wsUploadStartTime = 0;
bool wsUploadInProgress = false;
// sendTXT can synchronously report disconnection. Return it to Rust after the
// transport call so the live policy state handles it without reentrant loans.
bool wsSending = false;
bool wsSendDisconnected = false;
uint8_t wsUploadClientNum = 255;  // 255 = no active upload client
size_t wsLastProgressSent = 0;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;

}  // namespace

// File listing page template - now using generated headers:
// - HomePageHtml (from html/HomePage.html)
// - FilesPageHeaderHtml (from html/FilesPageHeader.html)
// - FilesPageFooterHtml (from html/FilesPageFooter.html)
struct WebFileBridge {
  const CrossPointWebServer& owner;
  explicit WebFileBridge(const CrossPointWebServer& owner) : owner(owner) {}
  static_assert(CrossPointWebServer::UploadState::UPLOAD_BUFFER_SIZE == XP_WEB_BUFFER_CAPACITY);
  HalFile files[3];
  HalFile* borrowed = nullptr;
  CrossPointWebServer::UploadState* uploadState = nullptr;
  CrossPointWebServer::FontUploadState* fontState = nullptr;
  CrossPointWebServer::PluginFsUploadState* pluginState = nullptr;
  bool websocket = false;
  String argument;
  JsonDocument paths;
  NetworkClient client;
  std::unique_ptr<uint8_t[]> streamBuffer;

  static XpWebText span(const String& value) {
    return {reinterpret_cast<const uint8_t*>(value.c_str()), value.length()};
  }
  static XpWebText span(const std::string& value) {
    return {reinterpret_cast<const uint8_t*>(value.c_str()), value.size()};
  }
  static XpWebText jsonString(void* ctx, const char* key) {
    const char* value = self(ctx).paths[key].as<const char*>();
    return {reinterpret_cast<const uint8_t*>(value), value ? strlen(value) : 0};
  }
  static void sendJsonLiteral(void* ctx, uint16_t status, const char* json) {
    self(ctx).owner.server->send(status, "application/json", json);
  }
  static XpWebText parseJson(void* ctx, XpWebText body) {
    const auto error = deserializeJson(self(ctx).paths, body.data, body.len);
    const char* message = error ? error.c_str() : nullptr;
    return {reinterpret_cast<const uint8_t*>(message), message ? strlen(message) : 0};
  }
  static XpWebText requestHeader(void* ctx, const char* name) {
    auto& b = self(ctx);
    b.argument = b.owner.server->header(name);
    return span(b.argument);
  }
  static void sendAsset(void* ctx, XpWebText asset) {
    self(ctx).owner.server->send_P(200, "text/html", reinterpret_cast<const char*>(asset.data), asset.len);
  }
  static String string(const XpWebText value) { return String(reinterpret_cast<const char*>(value.data), value.len); }
  static WebFileBridge& self(void* ctx) { return *static_cast<WebFileBridge*>(ctx); }
  static void route(const CrossPointWebServer& owner, XpWebRoute route) {
    WebFileBridge bridge{owner};
    xp_web_files(&bridge, &services, route);
  }
  String& text(uint32_t field) {
    switch (static_cast<XpWebStoredText>(field)) {
      case XP_WEB_UPLOAD_NAME:
        return uploadState->fileName;
      case XP_WEB_UPLOAD_PATH:
        return uploadState->path;
      case XP_WEB_UPLOAD_ERROR:
        return uploadState->error;
      case XP_WEB_WS_NAME:
        return wsUploadFileName;
      case XP_WEB_WS_PATH:
        return wsUploadPath;
      case XP_WEB_WS_COMPLETE_NAME:
        return wsLastCompleteName;
      case XP_WEB_FONT_FAMILY:
      case XP_WEB_FONT_PATH:
      case XP_WEB_PLUGIN_PATH:
      case XP_WEB_PLUGIN_TMP:
        break;
    }
    return argument;
  }
  static void stream(const CrossPointWebServer& owner, HalFile& file) {
    WebFileBridge bridge(owner);
    bridge.borrowed = &file;
    xp_web_stream(&bridge, &services);
  }
  static void http(const CrossPointWebServer&, CrossPointWebServer::UploadState&, uint32_t);
  static void ws(const CrossPointWebServer&, uint8_t, uint32_t, uint8_t*, size_t);
  static const XpWebTransfers transfers;
  static const XpWebServices services;
};

const XpWebServices WebFileBridge::services = {
    .argument = [](void* ctx, const char* key) -> XpWebText {
      auto& b = self(ctx);
      if (!key) {
        b.argument = String();
        return {nullptr, 0};
      }
      if (!*key)
        b.argument = b.owner.server->uri();
      else if (b.owner.server->hasArg(key))
        b.argument = b.owner.server->arg(key);
      else
        return {nullptr, 0};
      return span(b.argument);
    },
    .send =
        [](void* ctx, uint16_t code, const char* type, XpWebText body) {
          auto& server = self(ctx).owner.server;
          String response = string(body);
          if (response.length() != body.len && strcmp(type, "application/json") == 0) {
            LOG_ERR("WEB", "OOM: JSON response");
            server->send(503, "application/json", "{\"error\":\"out of memory\"}");
            return;
          }
          server->send(code, type, response);
        },
    .header =
        [](void* ctx, const char* name, XpWebText value) {
          self(ctx).owner.server->sendHeader(name, string(value), strcmp(name, "Location") == 0);
        },
    .chunk =
        [](void* ctx, XpWebText value) {
          self(ctx).owner.server->sendContent(reinterpret_cast<const char*>(value.data), value.len);
        },
    .length = [](void* ctx, size_t size) { self(ctx).owner.server->setContentLength(size); },
    .io = [](void* ctx, uint32_t operation, uint32_t slot, const char* path, uint8_t* data, size_t length) -> size_t {
      auto& b = self(ctx);
      if (slot >= 3) return 0;
      auto& file = slot == 0 && b.borrowed ? *b.borrowed : b.files[slot];
      switch (static_cast<XpWebIo>(operation)) {
        case XP_WEB_EXISTS:
          return Storage.exists(path);
        case XP_WEB_OPEN:
          file = Storage.open(path);
          return static_cast<bool>(file);
        case XP_WEB_CLOSE:
          file.close();
          return 0;
        case XP_WEB_IS_OPEN:
          return static_cast<bool>(file);
        case XP_WEB_IS_DIR:
          return file.isDirectory();
        case XP_WEB_SIZE:
          return file.size();
        case XP_WEB_NEXT:
          b.files[2] = file.openNextFile();
          return static_cast<bool>(b.files[2]);
        case XP_WEB_NAME:
          return file.getName(reinterpret_cast<char*>(data), length);
        case XP_WEB_READ:
          return static_cast<size_t>(file.read(data, length));
        case XP_WEB_WRITE:
          return file.write(data, length);
        case XP_WEB_MKDIR:
          return Storage.mkdir(path);
        case XP_WEB_REMOVE:
          return Storage.remove(path);
        case XP_WEB_RMDIR:
          return Storage.rmdir(path);
        case XP_WEB_CLIENT_WRITE:
          return b.client.write(data, length);
        case XP_WEB_CLIENT_CLEAR:
          b.client.clear();
          return 0;
        case XP_WEB_CREATE:
          return Storage.openFileForWrite(b.pluginState ? "PLG" : b.websocket ? "WS" : "WEB", path, file);
        case XP_WEB_AVAILABLE:
          return file.available();
        case XP_WEB_ENSURE_DIR:
          Storage.ensureDirectoryExists(path);
          return 0;
        case XP_WEB_REPLACE:
          return Storage.replaceFile(path, reinterpret_cast<const char*>(data));
        case XP_WEB_OPEN_READ:
          return Storage.openFileForRead("WEB", path, file);
      }
      return 0;
    },
    .sensitive = [](void*, const char* path) { return protectedpaths::isSensitivePath(path); },
    .book =
        [](void*, uint32_t operation, const char* path, const char* target) {
          switch (static_cast<XpWebBook>(operation)) {
            case XP_WEB_CACHE:
              clearBookCache(path);
              return true;
            case XP_WEB_REMOVE_BOOK:
              return removeBookFile(path);
            case XP_WEB_MOVE_BOOK:
              return ClippingStore::moveBook(path, target);
            case XP_WEB_LIBRARY_BOOK:
              if (library::isLibraryBookFile(path)) library::markLibraryIndexDirty();
              return true;
          }
          return false;
        },
    .buffer = [](void* ctx, bool allocate) -> uint8_t* {
      auto& b = self(ctx);
      auto& buffer = b.fontState ? b.fontState->buffer : b.uploadState ? b.uploadState->buffer : b.streamBuffer;
      if (allocate && !b.uploadState && !b.fontState) b.client = b.owner.server->client();
      if (allocate)
        buffer = makeUniqueNoThrow<uint8_t[]>(XP_WEB_BUFFER_CAPACITY);
      else
        buffer.reset();
      return buffer.get();
    },
    .watchdog = [](void*) { resetTaskWatchdogIfSubscribed(); },
    .yield = [](void*) { yield(); },
    .flags = [](void* ctx) -> uint32_t {
      const auto& b = self(ctx);
      return (b.owner.apMode ? 1u : 0u) | (b.owner.server && b.owner.server->method() == HTTP_OPTIONS ? 2u : 0u) |
             (SETTINGS.showHiddenFiles ? 4u : 0u) | (b.owner.running && b.owner.server ? 8u : 0u) |
             (b.owner.server && b.owner.server->client().connected() ? 16u : 0u) | (SETTINGS.pluginsEnabled ? 32u : 0u);
    },
    .paths = [](void* ctx, bool multiple) -> ptrdiff_t {
      auto& b = self(ctx);
      if (multiple) {
        if (deserializeJson(b.paths, b.owner.server->arg("paths"))) return -1;
      } else
        b.paths.add(b.owner.server->arg("path"));
      const auto paths = b.paths.as<JsonArray>();
      return paths.isNull() || paths.size() == 0 ? -2 : static_cast<ptrdiff_t>(paths.size());
    },
    .path = [](void* ctx, size_t index) -> XpWebText {
      auto& b = self(ctx);
      b.argument = b.paths[index].as<String>();
      return span(b.argument);
    },
    .diagnostic =
        [](void*, uint32_t event, XpWebText a, XpWebText b) {
          const char* x = reinterpret_cast<const char*>(a.data);
          const char* y = reinterpret_cast<const char*>(b.data);
          switch (static_cast<XpWebDiagnostic>(event)) {
            case XP_WEB_LIST_OPEN:
              LOG_DBG("WEB", "Failed to open directory: %.*s", int(a.len), x);
              break;
            case XP_WEB_LIST_NOT_DIR:
              LOG_DBG("WEB", "Not a directory: %.*s", int(a.len), x);
              break;
            case XP_WEB_LIST_SCAN:
              LOG_DBG("WEB", "Scanning files in: %.*s", int(a.len), x);
              break;
            case XP_WEB_LIST_SKIP:
              LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %.*s", int(a.len), x);
              break;
            case XP_WEB_LIST_DONE:
              LOG_DBG("WEB", "Served file listing page for path: %.*s", int(a.len), x);
              break;
            case XP_WEB_STREAM_OOM:
              LOG_ERR("WEB", "OOM: %u byte stream buffer", 4096u);
              break;
            case XP_WEB_FOLDER_UNSAFE:
              LOG_DBG("WEB", "Rejected unsafe folder name: %.*s", int(a.len), x);
              break;
            case XP_WEB_FOLDER_PROTECTED:
              LOG_DBG("WEB", "Rejected protected folder name: %.*s", int(a.len), x);
              break;
            case XP_WEB_FOLDER_CREATE:
              LOG_DBG("WEB", "Creating folder: %.*s", int(a.len), x);
              break;
            case XP_WEB_FOLDER_DONE:
              LOG_DBG("WEB", "Folder created successfully: %.*s", int(a.len), x);
              break;
            case XP_WEB_FOLDER_FAILED:
              LOG_DBG("WEB", "Failed to create folder: %.*s", int(a.len), x);
              break;
            case XP_WEB_RENAME_DONE:
              LOG_DBG("WEB", "Renamed file: %.*s -> %.*s", int(a.len), x, int(b.len), y);
              break;
            case XP_WEB_RENAME_FAILED:
              LOG_ERR("WEB", "Failed to rename file: %.*s -> %.*s", int(a.len), x, int(b.len), y);
              break;
            case XP_WEB_MOVE_DONE:
              LOG_DBG("WEB", "Moved file: %.*s -> %.*s", int(a.len), x, int(b.len), y);
              break;
            case XP_WEB_MOVE_FAILED:
              LOG_ERR("WEB", "Failed to move file: %.*s -> %.*s", int(a.len), x, int(b.len), y);
              break;
            case XP_WEB_OOM:
              LOG_ERR("WEB", "OOM: web file request");
              break;
          }
        },
    .memory_error = [](void*) { return tr(STR_MEMORY_ERROR); }};

CrossPointWebServer::CrossPointWebServer() { xp_web_job_init(&pluginJobs); }

CrossPointWebServer::~CrossPointWebServer() { stop(); }

void CrossPointWebServer::begin() {
  if (running) {
    LOG_DBG("WEB", "Web server already running");
    return;
  }

  // Check if we have a valid network connection (either STA connected or AP mode)
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);  // AP is running

  if (!isStaConnected && !isInApMode) {
    LOG_DBG("WEB", "Cannot start webserver - no valid network (mode=%d, status=%d)", wifiMode, WiFi.status());
    return;
  }

  // Store AP mode flag for later use (e.g., in handleStatus)
  apMode = isInApMode;

  LOG_DBG("WEB", "[MEM] Free heap before begin: %d bytes", ESP.getFreeHeap());
  LOG_DBG("WEB", "Network mode: %s", apMode ? "AP" : "STA");

  LOG_DBG("WEB", "Creating web server on port %d...", port);
  server.reset(new CrossPointHttpServer(port));

  // Disable WiFi sleep to improve responsiveness and prevent 'unreachable' errors.
  // This is critical for reliable web server operation on ESP32.
  WiFi.setSleep(false);
  // Default varies by ESP32 core version. The activity's loss-recovery loop
  // relies on driver retries during transient disconnects.
  WiFi.setAutoReconnect(true);

  // Note: WebServer class doesn't have setNoDelay() in the standard ESP32 library.
  // We rely on disabling WiFi sleep for responsiveness.

  LOG_DBG("WEB", "[MEM] Free heap after WebServer allocation: %d bytes", ESP.getFreeHeap());

  if (!server) {
    LOG_ERR("WEB", "Failed to create WebServer!");
    return;
  }

  // Add Access-Control-Allow-* headers to every response so web-based clients
  // and PWAs on other origins can use the HTTP API. Preflight OPTIONS requests
  // are answered in handleNotFound().
  server->enableCORS(true);

  // Setup routes
  LOG_DBG("WEB", "Setting up routes...");
  server->on("/", HTTP_GET, [this] { handleRoot(); });
  server->on("/files", HTTP_GET, [this] { handleFileList(); });
  server->on("/js/jszip.min.js", HTTP_GET, [this] { handleJszip(); });

  server->on("/assets/web.css", HTTP_GET,
             [this] { sendStaticContent(server.get(), webCss, sizeof(webCss), webCssETag, "text/css"); });
  server->on("/assets/icons.svg", HTTP_GET,
             [this] { sendStaticContent(server.get(), iconsSvg, sizeof(iconsSvg), iconsSvgETag, "image/svg+xml"); });
  server->on("/assets/crosspoint-icon.svg", HTTP_GET, [this] {
    sendStaticContent(server.get(), crosspoint_iconSvg, sizeof(crosspoint_iconSvg), crosspoint_iconSvgETag,
                      "image/svg+xml");
  });

  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
  server->on("/download", HTTP_GET, [this] { handleDownload(); });

  // Upload endpoint with special handling for multipart form data
  server->on("/upload", HTTP_POST, [this] { handleUploadPost(upload); }, [this] { handleUpload(upload); });

  // Create folder endpoint
  server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

  // Rename file endpoint
  server->on("/rename", HTTP_POST, [this] { handleRename(); });

  // Move file endpoint
  server->on("/move", HTTP_POST, [this] { handleMove(); });

  // Delete file/folder endpoint
  server->on("/delete", HTTP_POST, [this] { handleDelete(); });

  // Settings endpoints
  server->on("/settings", HTTP_GET, [this] { handleSettingsPage(); });
  server->on("/api/settings", HTTP_GET, [this] { handleGetSettings(); });
  server->on("/api/settings", HTTP_POST, [this] { handlePostSettings(); });

  // Font management endpoints
  server->on("/fonts", HTTP_GET, [this] { handleFontsPage(); });
  server->on("/api/fonts", HTTP_GET, [this] { handleFontList(); });
  server->on("/api/fonts/upload", HTTP_POST, [this] { handleFontUpload(); }, [this] { handleFontUploadData(); });
  server->on("/api/fonts/delete", HTTP_POST, [this] { handleFontDelete(); });

  // OPDS server endpoints
  server->on("/api/opds", HTTP_GET, [this] { handleGetOpdsServers(); });
  server->on("/api/opds", HTTP_POST, [this] { handlePostOpdsServer(); });
  server->on("/api/opds/delete", HTTP_POST, [this] { handleDeleteOpdsServer(); });

  // Browser-side plugins (JS on the SD card) + their generic capabilities.
  server->on("/api/plugins", HTTP_GET, [this] { handlePluginList(); });
  server->on("/plugin", HTTP_GET, [this] { handlePluginFile(); });
  server->on("/plugins-run", HTTP_GET, [this] { handlePluginRunnerPage(); });
  server->on("/api/plugin-jobs", HTTP_POST, [this] { handlePluginJobSubmit(); });
  server->on("/api/plugin-jobs/claim", HTTP_GET, [this] { handlePluginJobClaim(); });
  server->on("/api/plugin-jobs/complete", HTTP_POST, [this] { handlePluginJobComplete(); });
  server->on("/api/plugin-jobs/status", HTTP_GET, [this] { handlePluginJobStatus(); });
  server->on("/api/relay", HTTP_POST, [this] { handleRelay(); });
  server->on("/api/crypto", HTTP_POST, [this] { handleCrypto(); });
  server->on("/api/fetch", HTTP_POST, [this] { handleFetch(); });
  server->on("/api/book-key", HTTP_POST, [this] { handleBookKey(); });
  server->on("/api/plugin-fs", HTTP_POST, [this] { handlePluginFs(); }, [this] { handlePluginFsUpload(); });

  // Wi-Fi credential endpoints
  server->on("/api/wifi", HTTP_GET, [this] { handleGetWifiNetworks(); });
  server->on("/api/wifi", HTTP_POST, [this] { handlePostWifiNetwork(); });
  server->on("/api/wifi/delete", HTTP_POST, [this] { handleDeleteWifiNetwork(); });

  server->onNotFound([this] { handleNotFound(); });
  LOG_DBG("WEB", "[MEM] Free heap after route setup: %d bytes", ESP.getFreeHeap());

  // Collect WebDAV headers and register handler
  // If-None-Match is collected so the static-page handlers can answer conditional GETs with 304
  const char* collectedHeaders[] = {"Depth",      "Destination", "Overwrite",    "If",
                                    "Lock-Token", "Timeout",     "If-None-Match"};
  server->collectHeaders(collectedHeaders, 7);
  server->addHandler(new WebDAVHandler());  // Note: WebDAVHandler will be deleted by WebServer when server is stopped
  LOG_DBG("WEB", "WebDAV handler initialized");

  server->begin();

  // Start WebSocket server for fast binary uploads
  LOG_DBG("WEB", "Starting WebSocket server on port %d...", wsPort);
  wsServer.reset(new WebSocketsServer(wsPort));
  wsInstance = const_cast<CrossPointWebServer*>(this);
  wsServer->begin();
  wsServer->onEvent(wsEventCallback);
  LOG_DBG("WEB", "WebSocket server started");

  udpActive = udp.begin(LOCAL_UDP_PORT);
  LOG_DBG("WEB", "Discovery UDP %s on port %d", udpActive ? "enabled" : "failed", LOCAL_UDP_PORT);

  // Do not subscribe the serving task to the task watchdog. Arduino WebServer
  // permits five-second client and ACK waits, which can consume the entire
  // default watchdog window on a weak connection. The interrupt watchdog still
  // catches hard CPU lockups, matching the rest of the application lifecycle.

  running = true;

  LOG_DBG("WEB", "Web server started on port %d", port);
  // Show the correct IP based on network mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  LOG_DBG("WEB", "Access at http://%s/", ipAddr.c_str());
  LOG_DBG("WEB", "WebSocket at ws://%s:%d/", ipAddr.c_str(), wsPort);
  LOG_DBG("WEB", "[MEM] Free heap after server.begin(): %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::suspendTransferServices() {
  // Leave the WebSocket server alone mid-upload; killing it would abort the
  // transfer. The fetch just stalls that upload until it completes.
  if (wsServer && !wsUploadInProgress) {
    wsServer->close();
    wsServer.reset();
  }
  if (udpActive) udp.stop();
  LOG_DBG("WEB", "Transfer services suspended, heap %u, max block %u", (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getMaxAllocHeap());
}

void CrossPointWebServer::resumeTransferServices() {
  if (!running) return;
  if (!wsServer) {
    auto* ws = new (std::nothrow) WebSocketsServer(wsPort);
    if (ws) {
      wsServer.reset(ws);
      wsServer->begin();
      wsServer->onEvent(wsEventCallback);
    } else {
      LOG_ERR("WEB", "OOM: WebSocket server restart");
    }
  }
  if (udpActive) udpActive = udp.begin(LOCAL_UDP_PORT);
  LOG_DBG("WEB", "Transfer services resumed, heap %u, max block %u", (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getMaxAllocHeap());
}

bool CrossPointWebServer::dropUploadIfCancelled() const {
  if (!uploadCancelCheck || !uploadCancelCheck()) return false;
  // WebServer's next read of the body then fails and it raises UPLOAD_FILE_ABORTED.
  server->client().stop();
  return true;
}

void CrossPointWebServer::abortWsUpload(const char*) { WebFileBridge::ws(*this, 255, 4, nullptr, 0); }

void CrossPointWebServer::stop() {
  if (!running || !server) {
    LOG_DBG("WEB", "stop() called but already stopped (running=%d, server=%p)", running, server.get());
    return;
  }

  LOG_DBG("WEB", "STOP INITIATED - setting running=false first");
  running = false;  // Set this FIRST to prevent handleClient from using server

  LOG_DBG("WEB", "[MEM] Free heap before stop: %d bytes", ESP.getFreeHeap());

  // Close any in-progress WebSocket upload and remove partial file
  if (wsUploadInProgress && wsUploadFile) {
    abortWsUpload("WEB");
  }

  // Stop WebSocket server
  if (wsServer) {
    LOG_DBG("WEB", "Stopping WebSocket server...");
    wsServer->close();
    wsServer.reset();
    wsInstance = nullptr;
    LOG_DBG("WEB", "WebSocket server stopped");
  }

  if (udpActive) {
    udp.stop();
    udpActive = false;
  }

  // Brief delay to allow any in-flight handleClient() calls to complete
  delay(20);

  server->stop();
  LOG_DBG("WEB", "[MEM] Free heap after server->stop(): %d bytes", ESP.getFreeHeap());

  // Brief delay before deletion
  delay(10);

  server.reset();
  LOG_DBG("WEB", "Web server stopped and deleted");
  LOG_DBG("WEB", "[MEM] Free heap after delete server: %d bytes", ESP.getFreeHeap());

  // Note: Static upload variables (uploadFileName, uploadPath, uploadError) are declared
  // later in the file and will be cleared when they go out of scope or on next upload
  LOG_DBG("WEB", "[MEM] Free heap final: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::handleClient() {
  static unsigned long lastDebugPrint = 0;

  // Check running flag FIRST before accessing server
  if (!running) {
    return;
  }

  // Double-check server pointer is valid
  if (!server) {
    LOG_DBG("WEB", "WARNING: handleClient called with null server!");
    return;
  }

  // Print debug every 10 seconds to confirm handleClient is being called
  if (millis() - lastDebugPrint > 10000) {
    LOG_DBG("WEB", "handleClient active, server running on port %d", port);
    lastDebugPrint = millis();
  }

  server->handleClient();
  // WebServer otherwise keeps the last request's argument strings allocated
  // until another request arrives. They are no longer observable once its
  // handler returns, so release them now instead of retaining a JSON body.
  releaseRequestArguments(server.get());

  // Handle WebSocket events
  if (wsServer) {
    wsServer->loop();
  }

  // Respond to discovery broadcasts
  if (udpActive) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[16];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "hello") == 0) {
          String hostname = WiFi.getHostname();
          if (hostname.isEmpty()) {
            hostname = "crosspoint";
          }
          String message = "crosspoint (on " + hostname + ");" + String(wsPort);
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
          udp.endPacket();
        }
      }
    }
  }
}

CrossPointWebServer::WsUploadStatus CrossPointWebServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

static void sendStaticContent(WebServer* server, const char* data, size_t len, const char* etag,
                              const char* contentType) {
  // Content is baked into flash at build time, so the ETag is stable for the
  // lifetime of a firmware image. Honor If-None-Match with a 304 so browsers
  // reuse their cache instead of re-downloading on every navigation.
  if (server->header("If-None-Match") == etag) {
    server->sendHeader("ETag", etag);
    server->sendHeader("Cache-Control", "no-cache");
    server->send(304);
    return;
  }
  server->sendHeader("Content-Encoding", "gzip");
  server->sendHeader("ETag", etag);
  // no-cache: the browser may cache, but must revalidate (conditional GET)
  // before reuse — this is what unlocks 304 responses.
  server->sendHeader("Cache-Control", "no-cache");
  server->send_P(200, contentType, data, len);
}

void CrossPointWebServer::handleRoot() const {
  sendStaticContent(server.get(), HomePageHtml, sizeof(HomePageHtml), HomePageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served root page");
}

void CrossPointWebServer::handleJszip() const {
  sendStaticContent(server.get(), jszip_minJs, jszip_minJsCompressedSize, jszip_minJsETag, "application/javascript");
  LOG_DBG("WEB", "Served jszip.min.js");
}

void CrossPointWebServer::handleNotFound() const { WebFileBridge::route(*this, XP_WEB_NOT_FOUND); }

void CrossPointWebServer::handleStatus() const {
  // Get correct IP based on AP vs STA mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  JsonDocument doc;
  doc["version"] = CROSSPOINT_VERSION;
  doc["firmwareFamily"] = firmwareidentity::FAMILY;
  doc["pluginApi"] = firmwareidentity::PLUGIN_API_VERSION;
  doc["pluginCapabilities"] = firmwareidentity::PLUGIN_CAPABILITIES;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  // ?plugin=<name> adds a stable device ID for that plugin: sha256(secret ||
  // name). Not reversible to any hardware ID, and different per plugin.
  String plugin = server->arg("plugin");
  plugin.toLowerCase();  // FAT folder names ignore case
  uint8_t secret[32];
  if (!plugin.isEmpty() && plugin.length() <= 64 && deviceSecret(secret)) {
    uint8_t input[32 + 64];
    memcpy(input, secret, sizeof(secret));
    memcpy(input + sizeof(secret), plugin.c_str(), plugin.length());
    uint8_t hash[32];
    if (wc_Sha256Hash(input, sizeof(secret) + plugin.length(), hash) == 0) {
      char hex[65];
      for (size_t i = 0; i < sizeof(hash); i++) snprintf(hex + 2 * i, 3, "%02x", hash[i]);
      doc["deviceId"] = hex;
    }
  }
#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
  doc["device"] = gpio.deviceIsX3() ? "X3" : "X4";
#else
  doc["device"] = BoardConfig::ACTIVE.name;
#endif

  char snBuf[33] = {0};
  bool valid = false;
#if !CONFIG_IDF_TARGET_ESP32
  // Classic ESP32's efuse table has no USER_DATA block (C3/S3 only)
  if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, snBuf, 256) == ESP_OK) {
    valid = snBuf[0] != '\0' && snBuf[0] != (char)0xFF;
    for (int i = 0; i < 32 && snBuf[i] != '\0'; i++) {
      if (!std::isprint(static_cast<unsigned char>(snBuf[i]))) {
        valid = false;
        break;
      }
    }
  }
#endif

  if (valid) {
    doc["serial"] = snBuf;
  } else {
    doc["serial"] = "Not found";
  }

  String response;
  serializeJson(doc, response);
  server->send(200, "application/json", response);
}

void CrossPointWebServer::handleFileList() const {
  sendStaticContent(server.get(), FilesPageHtml, sizeof(FilesPageHtml), FilesPageHtmlETag, "text/html");
}

void CrossPointWebServer::handleFileListData() const { WebFileBridge::route(*this, XP_WEB_LIST); }

void CrossPointWebServer::streamFileToClient(HalFile& file) const { WebFileBridge::stream(*this, file); }

void CrossPointWebServer::handleDownload() const { WebFileBridge::route(*this, XP_WEB_DOWNLOAD); }

// Diagnostic counters for upload performance analysis
static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

const XpWebTransfers WebFileBridge::transfers = {
    .files = &WebFileBridge::services,
    .get = [](void* ctx, uint32_t field) -> XpWebText {
      auto& b = self(ctx);
      if (field == XP_WEB_FONT_FAMILY) return span(b.fontState->familyName);
      if (field == XP_WEB_FONT_PATH) return span(b.fontState->filePath);
      if (field == XP_WEB_PLUGIN_PATH) return span(b.pluginState->path);
      if (field == XP_WEB_PLUGIN_TMP) return span(b.pluginState->tmp);
      return span(b.text(field));
    },
    .set =
        [](void* ctx, uint32_t field, XpWebText value) {
          auto& b = self(ctx);
          if (field == XP_WEB_FONT_FAMILY || field == XP_WEB_FONT_PATH) {
            auto& target = field == XP_WEB_FONT_FAMILY ? b.fontState->familyName : b.fontState->filePath;
            target.assign(value.len ? reinterpret_cast<const char*>(value.data) : "", value.len);
          } else if (field == XP_WEB_PLUGIN_PATH || field == XP_WEB_PLUGIN_TMP) {
            auto& target = field == XP_WEB_PLUGIN_PATH ? b.pluginState->path : b.pluginState->tmp;
            target.assign(value.len ? reinterpret_cast<const char*>(value.data) : "", value.len);
          } else
            b.text(field) = string(value);
        },
    .joined_path = [](void* ctx, bool ws) -> XpWebText {
      auto& b = self(ctx);
      const auto& parent = ws ? wsUploadPath : b.uploadState->path;
      const auto& name = ws ? wsUploadFileName : b.uploadState->fileName;
      if (!b.argument.reserve(parent.length() + name.length() + 2)) return {nullptr, 0};
      b.argument = parent;
      if (!b.argument.endsWith("/")) b.argument += "/";
      b.argument += name;
      return span(b.argument);
    },
    .time = [](void*) -> uint32_t { return millis(); },
    .cancel = [](void* ctx) { return self(ctx).owner.dropUploadIfCancelled(); },
    .diagnostic =
        [](void*, uint32_t event, XpWebText x, XpWebText y, size_t a, size_t b, size_t c) {
          const char* first = x.data ? reinterpret_cast<const char*>(x.data) : "";
          const char* second = reinterpret_cast<const char*>(y.data);
          const float kbps = b > 0 ? (a / 1024.0) / (b / 1000.0) : 0;
          switch (static_cast<XpWebTransferDiagnostic>(event)) {
            case XP_WEB_UPLOAD_STOPPED:
              LOG_DBG("WEB", "[UPLOAD] ERROR: handleUpload called but server not running!");
              break;
            case XP_WEB_UPLOAD_UNSAFE:
              LOG_DBG("WEB", "[UPLOAD] Rejected unsafe filename: %.*s", int(x.len), first);
              break;
            case XP_WEB_UPLOAD_START:
              LOG_DBG("WEB", "[UPLOAD] START: %.*s to path: %.*s", int(x.len), first, int(y.len), second);
              break;
            case XP_WEB_UPLOAD_HEAP:
              LOG_DBG("WEB", "[UPLOAD] Free heap: %d bytes", ESP.getFreeHeap());
              break;
            case XP_WEB_UPLOAD_COLLISION:
              LOG_DBG("WEB", "[UPLOAD] Collision: %.*s", int(x.len), first);
              break;
            case XP_WEB_UPLOAD_OOM:
              LOG_ERR("WEB", "OOM: upload buffer");
              break;
            case XP_WEB_UPLOAD_CREATE_FAILED:
              LOG_DBG("WEB", "[UPLOAD] FAILED to create file: %.*s", int(x.len), first);
              break;
            case XP_WEB_UPLOAD_CREATED:
              LOG_DBG("WEB", "[UPLOAD] File created successfully: %.*s", int(x.len), first);
              break;
            case XP_WEB_UPLOAD_FLUSH_FAILED:
              LOG_DBG("WEB", "[UPLOAD] Buffer flush failed: expected %d, wrote %d", a, b);
              break;
            case XP_WEB_UPLOAD_PROGRESS:
              LOG_DBG("WEB", "[UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes", a, a / 1024.0, kbps, c);
              break;
            case XP_WEB_UPLOAD_COMPLETE:
              LOG_DBG("WEB", "[UPLOAD] Complete: %.*s (%d bytes in %lu ms, avg %.1f KB/s)", int(x.len), first, a,
                      static_cast<unsigned long>(b), kbps);
              break;
            case XP_WEB_UPLOAD_DIAGNOSTICS: {
              const float percent = c > 0 ? b * 100.0 / c : 0;
              LOG_DBG("WEB", "[UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)", a,
                      static_cast<unsigned long>(b), percent);
              break;
            }
            case XP_WEB_UPLOAD_ABORTED:
              LOG_DBG("WEB", "Upload aborted");
              break;
            case XP_WEB_WS_DISCONNECT:
              LOG_DBG("WS", "Client %u disconnected", unsigned(a));
              break;
            case XP_WEB_WS_CONNECT:
              LOG_DBG("WS", "Client %u connected", unsigned(a));
              break;
            case XP_WEB_WS_TEXT:
              LOG_DBG("WS", "Text from client %u: %.*s", unsigned(a), int(x.len), first);
              break;
            case XP_WEB_WS_UNSAFE:
              LOG_DBG("WS", "START rejected: invalid filename '%.*s'", int(x.len), first);
              break;
            case XP_WEB_WS_BAD_SIZE:
              LOG_DBG("WS", "START rejected: invalid size token '%.*s'", int(x.len), first);
              break;
            case XP_WEB_WS_COLLISION:
              LOG_DBG("WS", "Upload collision: %.*s", int(x.len), first);
              break;
            case XP_WEB_WS_START:
              LOG_DBG("WS", "Starting upload: %.*s (%d bytes) to %.*s", int(x.len), first, a, int(y.len), second);
              break;
            case XP_WEB_WS_EMPTY:
              LOG_DBG("WS", "Zero-byte upload complete: %.*s", int(x.len), first);
              break;
            case XP_WEB_WS_COMPLETE:
              LOG_DBG("WS", "Upload complete: %.*s (%d bytes in %lu ms, %.1f KB/s)", int(x.len), first, a,
                      static_cast<unsigned long>(b), kbps);
              break;
            case XP_WEB_WS_REMOVED:
              LOG_DBG(y.len == 3 ? "WEB" : "WS", "Deleted incomplete upload: %.*s", int(x.len), first);
              break;
            case XP_WEB_WS_REMOVE_FAILED:
              LOG_DBG(y.len == 3 ? "WEB" : "WS", "Failed to delete incomplete upload: %.*s", int(x.len), first);
              break;
          }
        },
    .ws_send =
        [](void* ctx, uint8_t client, XpWebText value) {
          wsSending = true;
          wsSendDisconnected = false;
          self(ctx).owner.wsServer->sendTXT(client, value.len ? value.data : reinterpret_cast<const uint8_t*>(""),
                                            value.len);
          wsSending = false;
          return wsSendDisconnected;
        },
    .ws_memory_error =
        [](void* ctx, uint8_t client) {
          auto& b = self(ctx);
          const char* message = tr(STR_MEMORY_ERROR);
          if (!b.argument.reserve(strlen(message) + 6)) {
            return transfers.ws_send(ctx, client, {reinterpret_cast<const uint8_t*>("ERROR:"), 6});
          }
          b.argument = "ERROR:";
          b.argument += message;
          return transfers.ws_send(ctx, client, span(b.argument));
        }};

void WebFileBridge::http(const CrossPointWebServer& owner, CrossPointWebServer::UploadState& state, uint32_t event) {
  static size_t lastLoggedSize = 0;
  WebFileBridge bridge(owner);
  bridge.borrowed = &state.file;
  bridge.uploadState = &state;
  XpWebUpload policy{state.size,
                     state.bufferPos,
                     writeCount,
                     lastLoggedSize,
                     static_cast<uint32_t>(uploadStartTime),
                     static_cast<uint32_t>(totalWriteTime),
                     state.buffer.get(),
                     state.success};
  XpWebText name{nullptr, 0};
  const uint8_t* bytes = nullptr;
  size_t length = 0;
  if (event != 4 && owner.running && owner.server) {
    const auto& part = owner.server->upload();
    name = span(part.filename);
    bytes = part.buf;
    length = part.currentSize;
  }
  xp_web_upload(&bridge, &transfers, &policy, event, name, bytes, length);
  state.size = policy.size;
  state.bufferPos = policy.buffer_pos;
  state.success = policy.success;
  writeCount = policy.writes;
  lastLoggedSize = policy.last_logged;
  uploadStartTime = policy.started_at;
  totalWriteTime = policy.write_time;
}

void WebFileBridge::ws(const CrossPointWebServer& owner, uint8_t client, uint32_t event, uint8_t* payload,
                       size_t length) {
  WebFileBridge bridge(owner);
  bridge.borrowed = &wsUploadFile;
  bridge.websocket = true;
  XpWebWsUpload state{wsUploadSize,
                      wsUploadReceived,
                      wsLastProgressSent,
                      wsLastCompleteSize,
                      static_cast<uint32_t>(wsUploadStartTime),
                      static_cast<uint32_t>(wsLastCompleteAt),
                      wsUploadClientNum,
                      wsUploadInProgress};
  xp_web_ws(&bridge, &transfers, &state, client, event, payload, length);
  wsUploadSize = state.total;
  wsUploadReceived = state.received;
  wsLastProgressSent = state.last_progress;
  wsLastCompleteSize = state.complete_size;
  wsUploadStartTime = state.started_at;
  wsLastCompleteAt = state.completed_at;
  wsUploadClientNum = state.client;
  wsUploadInProgress = state.active;
}

// Drop a partially written upload so a truncated book never lands in the library.
// The file is new: uploads refuse to overwrite an existing name.

void CrossPointWebServer::handleUpload(UploadState& state) const {
  const uint32_t event = running && server ? static_cast<uint32_t>(server->upload().status) : 0;
  WebFileBridge::http(*this, state, event);
}

void CrossPointWebServer::handleUploadPost(UploadState& state) const { WebFileBridge::http(*this, state, 4); }

void CrossPointWebServer::handleCreateFolder() const { WebFileBridge::route(*this, XP_WEB_MKDIR_ROUTE); }

void CrossPointWebServer::handleRename() const { WebFileBridge::route(*this, XP_WEB_RENAME); }

void CrossPointWebServer::handleMove() const { WebFileBridge::route(*this, XP_WEB_MOVE); }

void CrossPointWebServer::handleDelete() const { WebFileBridge::route(*this, XP_WEB_DELETE); }

struct WebConfigBridge : WebFileBridge {
  explicit WebConfigBridge(const CrossPointWebServer& owner) : WebFileBridge(owner) {}
  std::vector<DictionaryEntry> dictionaries;
  std::vector<SettingInfo> settings;
  std::vector<WifiCredentialSummary> networks;
  std::optional<WifiCredential> credential;
  std::optional<std::string> ssid;
  std::string scratch;
  static WebConfigBridge& config(void* ctx) { return static_cast<WebConfigBridge&>(self(ctx)); }
  static XpWebText view(const std::string& value) {
    return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
  }
  static XpWebText view(const char* value) {
    return {reinterpret_cast<const uint8_t*>(value), value ? strlen(value) : 0};
  }
  static std::string copy(XpWebText value) {
    return std::string(value.len ? reinterpret_cast<const char*>(value.data) : "", value.len);
  }
  static void route(const CrossPointWebServer& owner, XpWebConfigRoute route) {
    WebConfigBridge bridge(owner);
    xp_web_config(static_cast<WebFileBridge*>(&bridge), &services, route);
  }
  static const XpWebConfig services;
};

const XpWebConfig WebConfigBridge::services = {
    .files = &WebFileBridge::services,
    .parse = parseJson,
    .json_value = [](void* ctx, const char* key, int32_t header, bool convert) -> XpWebJsonValue {
      auto& b = config(ctx);
      const JsonVariantConst value =
          header < 0 ? b.paths[key].as<JsonVariantConst>() : b.paths["headers"][header][key].as<JsonVariantConst>();
      const bool present =
          header < 0 ? b.paths[key].is<JsonVariant>() : b.paths["headers"][header][key].is<JsonVariant>();
      XpWebText text{nullptr, 0};
      if (convert && present) {
        b.scratch = value.as<std::string>();
        text = view(b.scratch);
      } else if (value.is<const char*>()) {
        const JsonString string = value.as<JsonString>();
        text = {reinterpret_cast<const uint8_t*>(string.c_str()), string.size()};
      }
      return {text, value.as<int>(), present, value.is<const char*>(), value.is<int>(), value.is<JsonArrayConst>()};
    },
    .count = [](void* ctx, uint32_t route) -> size_t {
      auto& b = config(ctx);
      switch (static_cast<XpWebConfigRoute>(route)) {
        case XP_WEB_SETTINGS_GET:
        case XP_WEB_SETTINGS_POST:
          DictionaryRegistry::discover(b.dictionaries);
          b.settings = getSettingsList(&sdFontSystem.registry(), &b.dictionaries);
          return b.settings.size();
        case XP_WEB_OPDS_GET:
        case XP_WEB_OPDS_POST:
        case XP_WEB_OPDS_DELETE:
          return OPDS_STORE.getCount();
        case XP_WEB_WIFI_GET:
          b.networks = WIFI_STORE.getCredentialSummaries();
          return b.networks.size();
        case XP_WEB_WIFI_POST:
        case XP_WEB_WIFI_DELETE:
        case XP_WEB_SETTINGS_PAGE:
          return 0;
      }
      return 0;
    },
    .setting =
        [](void* ctx, size_t index, bool readValue, XpWebSetting* out) {
          auto& b = config(ctx);
          const auto& s = b.settings[index];
          *out = {};
          out->key = view(s.key);
          out->hidden = s.webHidden;
          out->name = view(I18N.get(s.nameId));
          out->category = view(I18N.get(s.category));
          out->obfuscated = s.obfuscated;
          switch (s.type) {
            case SettingType::TOGGLE:
              out->kind = XP_WEB_TOGGLE;
              break;
            case SettingType::ENUM:
              out->kind = XP_WEB_ENUM;
              break;
            case SettingType::VALUE:
              out->kind = XP_WEB_VALUE;
              break;
            case SettingType::STRING:
              out->kind = XP_WEB_STRING;
              break;
            default:
              out->kind = XP_WEB_ACTION;
              break;
          }
          if (s.type == SettingType::ENUM) {
            const auto labels = s.enumLabels();
            out->options = s.enumStringValues.empty() ? labels.size() : s.enumStringValues.size();
            out->off_on = s.enumStringValues.empty() && labels.size() == 2 && labels[0] == StrId::STR_STATE_OFF &&
                          labels[1] == StrId::STR_STATE_ON;
          }
          out->min = s.valueRange.min;
          out->max = s.valueRange.max;
          out->step = s.valueRange.step;
          if (!readValue) return;
          if (s.type == SettingType::STRING) {
            if (s.stringGetter) {
              b.scratch = s.stringGetter();
              out->text = view(b.scratch);
              out->has_value = true;
            } else if (s.stringMaxLen > 0) {
              out->text = view(reinterpret_cast<const char*>(&SETTINGS) + s.stringOffset);
              out->has_value = true;
            }
          } else if (s.valuePtr) {
            out->value = SETTINGS.*(s.valuePtr);
            out->has_value = true;
          } else if (s.type == SettingType::ENUM && s.valueGetter) {
            out->value = s.valueGetter();
            out->has_value = true;
          }
        },
    .option =
        [](void* ctx, size_t index, size_t option) {
          const auto& s = config(ctx).settings[index];
          return s.enumStringValues.empty() ? view(I18N.get(s.enumLabels()[option])) : view(s.enumStringValues[option]);
        },
    .set_setting =
        [](void* ctx, size_t index, int32_t value) {
          const auto& b = config(ctx);
          const auto& s = b.settings[index];
          if (s.type == SettingType::STRING) {
            if (s.stringSetter)
              s.stringSetter(b.scratch);
            else if (s.stringMaxLen > 0) {
              char* ptr = reinterpret_cast<char*>(&SETTINGS) + s.stringOffset;
              strncpy(ptr, b.scratch.c_str(), s.stringMaxLen - 1);
              ptr[s.stringMaxLen - 1] = '\0';
            }
          } else if (s.valuePtr)
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(value);
          else if (s.type == SettingType::ENUM && s.valueSetter)
            s.valueSetter(static_cast<uint8_t>(value));
        },
    .save_settings = [](void*) { SETTINGS.saveToFile(); },
    .opds =
        [](void*, size_t index, XpWebOpds* out) {
          const auto* s = OPDS_STORE.getServer(index);
          if (!s) return false;
          *out = {view(s->name), view(s->url), view(s->username), view(s->password), {}};
          for (size_t i = 0; i < OpdsServer::MAX_CUSTOM_HEADERS; ++i)
            out->headers[i] = {view(s->customHeaders[i].name), view(s->customHeaders[i].value)};
          return true;
        },
    .put_opds =
        [](void*, int32_t index, const XpWebOpds* value) {
          OpdsServer s;
          s.name = copy(value->name);
          s.url = copy(value->url);
          s.username = copy(value->username);
          s.password = copy(value->password);
          for (size_t i = 0; i < OpdsServer::MAX_CUSTOM_HEADERS; ++i) {
            s.customHeaders[i].name = copy(value->headers[i].name);
            s.customHeaders[i].value = copy(value->headers[i].value);
          }
          if (index < 0) return OPDS_STORE.addServer(s);
          OPDS_STORE.updateServer(static_cast<size_t>(index), s);
          return true;
        },
    .remove_opds = [](void*, size_t index) { OPDS_STORE.removeServer(index); },
    .wifi =
        [](void* ctx, size_t index, uint32_t read, XpWebWifi* out) {
          auto& b = config(ctx);
          *out = {};
          switch (static_cast<XpWebWifiRead>(read)) {
            case XP_WEB_WIFI_SUMMARY: {
              const auto& s = b.networks[index];
              *out = {view(s.ssid), {}, s.hasPassword, s.isLastConnected};
              return true;
            }
            case XP_WEB_WIFI_CREDENTIAL:
              b.credential = WIFI_STORE.getCredentialAt(index);
              if (!b.credential) return false;
              out->ssid = view(b.credential->ssid);
              out->password = view(b.credential->password);
              return true;
            case XP_WEB_WIFI_SSID:
              b.ssid = WIFI_STORE.getSsidAt(index);
              if (!b.ssid) return false;
              out->ssid = view(*b.ssid);
              return true;
          }
          return false;
        },
    .put_wifi = [](void*, XpWebText ssid,
                   XpWebText password) { return WIFI_STORE.addCredential(copy(ssid), copy(password)); },
    .remove_wifi = [](void*, XpWebText ssid) { return WIFI_STORE.removeCredential(copy(ssid)); },
    .page = {requestHeader,
             sendAsset,
             {reinterpret_cast<const uint8_t*>(SettingsPageHtml), sizeof(SettingsPageHtml)},
             {reinterpret_cast<const uint8_t*>(SettingsPageHtmlETag),
              std::char_traits<char>::length(SettingsPageHtmlETag)}},
    .diagnostic =
        [](void*, uint32_t event, size_t count, XpWebText text) {
          const char* value = text.len ? reinterpret_cast<const char*>(text.data) : "";
          switch (static_cast<XpWebConfigDiagnostic>(event)) {
            case XP_WEB_CONFIG_PAGE:
              LOG_DBG("WEB", "Served settings page");
              break;
            case XP_WEB_CONFIG_SETTINGS:
              LOG_DBG("WEB", "Served settings API");
              break;
            case XP_WEB_CONFIG_SKIPPED:
              LOG_DBG("WEB", "Skipping oversized setting JSON for: %.*s", int(text.len), value);
              break;
            case XP_WEB_CONFIG_APPLIED:
              LOG_DBG("WEB", "Applied %d setting(s)", int(count));
              break;
            case XP_WEB_CONFIG_OPDS_LIST:
              LOG_DBG("WEB", "Served OPDS servers API (%zu servers)", count);
              break;
            case XP_WEB_CONFIG_OPDS_UPDATED:
              LOG_DBG("WEB", "Updated OPDS server at index %d", int(count));
              break;
            case XP_WEB_CONFIG_OPDS_ADDED:
              LOG_DBG("WEB", "Added new OPDS server: %.*s", int(text.len), value);
              break;
            case XP_WEB_CONFIG_OPDS_DELETED:
              LOG_DBG("WEB", "Deleted OPDS server at index %d", int(count));
              break;
            case XP_WEB_CONFIG_WIFI_LIST:
              LOG_DBG("WEB", "Served Wi-Fi credentials API (%zu network(s))", count);
              break;
            case XP_WEB_CONFIG_WIFI_UPDATED:
              LOG_DBG("WEB", "Updated Wi-Fi network at index %d (SSID: %.*s)", int(count), int(text.len), value);
              break;
            case XP_WEB_CONFIG_WIFI_ADDED:
              LOG_DBG("WEB", "Added Wi-Fi network: %.*s", int(text.len), value);
              break;
            case XP_WEB_CONFIG_WIFI_DELETED:
              LOG_DBG("WEB", "Deleted Wi-Fi network at index %d (SSID: %.*s)", int(count), int(text.len), value);
              break;
          }
        }};
static_assert(OpdsServer::MAX_CUSTOM_HEADERS == 2);

void CrossPointWebServer::handleSettingsPage() const { WebConfigBridge::route(*this, XP_WEB_SETTINGS_PAGE); }

void CrossPointWebServer::handleGetSettings() const { WebConfigBridge::route(*this, XP_WEB_SETTINGS_GET); }

void CrossPointWebServer::handlePostSettings() { WebConfigBridge::route(*this, XP_WEB_SETTINGS_POST); }

void CrossPointWebServer::handleGetOpdsServers() const { WebConfigBridge::route(*this, XP_WEB_OPDS_GET); }

void CrossPointWebServer::handlePostOpdsServer() { WebConfigBridge::route(*this, XP_WEB_OPDS_POST); }

void CrossPointWebServer::handleDeleteOpdsServer() { WebConfigBridge::route(*this, XP_WEB_OPDS_DELETE); }

void CrossPointWebServer::handleGetWifiNetworks() const { WebConfigBridge::route(*this, XP_WEB_WIFI_GET); }

void CrossPointWebServer::handlePostWifiNetwork() { WebConfigBridge::route(*this, XP_WEB_WIFI_POST); }

void CrossPointWebServer::handleDeleteWifiNetwork() { WebConfigBridge::route(*this, XP_WEB_WIFI_DELETE); }

// ---------------------------------------------------------------------------
// Browser-side plugins (JS on the SD card)
// ---------------------------------------------------------------------------

bool CrossPointWebServer::readJsonBody(JsonDocument& out) const {
  if (!server->hasArg("plain")) {
    server->send(400, "application/json", "{\"error\":\"missing body\"}");
    return false;
  }
  if (deserializeJson(out, server->arg("plain")) != DeserializationError::Ok) {
    server->send(400, "application/json", "{\"error\":\"bad json\"}");
    return false;
  }
  return true;
}

void CrossPointWebServer::sendJson(const JsonDocument& doc) const {
  String out;
  if (!out.reserve(measureJson(doc))) {
    LOG_ERR("WEB", "OOM: JSON response");
    server->send(503, "application/json", "{\"error\":\"out of memory\"}");
    return;
  }
  serializeJson(doc, out);
  server->send(200, "application/json", out);
}

bool CrossPointWebServer::rejectIfPluginsDisabled() const {
  if (SETTINGS.pluginsEnabled) return false;
  server->send(403, "text/plain", "plugin system disabled");
  return true;
}

struct WebPluginBridge : WebFileBridge {
  explicit WebPluginBridge(const CrossPointWebServer& owner) : WebFileBridge(owner) {}
  std::vector<PluginLocations::Entry> entries;
  std::string scratch;
  static WebPluginBridge& plugins(void* ctx) { return static_cast<WebPluginBridge&>(self(ctx)); }
  static void route(const CrossPointWebServer& owner, XpWebPluginRoute route) {
    WebPluginBridge bridge(owner);
    xp_web_plugins(static_cast<WebFileBridge*>(&bridge), &services, route);
  }
  static void upload(CrossPointWebServer& owner, uint32_t event) {
    WebFileBridge bridge(owner);
    auto& s = owner.pluginFsUpload;
    bridge.pluginState = &s;
    bridge.borrowed = &s.file;
    XpWebPluginUpload state{s.bytes, s.error, static_cast<uint16_t>(s.errorStatus), s.started};
    if (event != UPLOAD_FILE_WRITE) {
      xp_web_plugin_upload(&bridge, &services, &state, event, nullptr, 0);
    } else {
      const auto& part = owner.server->upload();
      xp_web_plugin_upload(&bridge, &services, &state, event, part.buf, part.currentSize);
    }
    s.bytes = state.bytes;
    s.error = state.error;
    s.errorStatus = state.status;
    s.started = state.started;
  }
  static const XpWebPlugins services;
};

const XpWebPlugins WebPluginBridge::services = {
    .transfers = &WebFileBridge::transfers,
    .parse = parseJson,
    .json_string = [](void* ctx, const char* key) -> XpWebText {
      const JsonString value = self(ctx).paths[key].as<JsonString>();
      return {reinterpret_cast<const uint8_t*>(value.c_str()), value.size()};
    },
    .expires = [](void* ctx) -> int64_t { return self(ctx).paths["expires"] | static_cast<int64_t>(0); },
    .scan = [](void* ctx) -> size_t {
      auto& b = plugins(ctx);
      b.entries = PluginLocations::scanPlugins();
      return b.entries.size();
    },
    .entry =
        [](void* ctx, size_t index, XpWebPluginEntry* out) {
          const auto& entry = plugins(ctx).entries[index];
          *out = {span(entry.name), span(entry.dir), entry.hasPluginJs, entry.hasManifest};
        },
    .find = [](void* ctx, const char* name) -> XpWebText {
      auto& b = plugins(ctx);
      b.scratch = PluginLocations::findPluginDir(name);
      return span(b.scratch);
    },
    .read = [](void* ctx, const char* path) -> XpWebText {
      auto& b = plugins(ctx);
      if (!path) {
        std::string{}.swap(b.scratch);
        b.paths.clear();
        return {nullptr, 0};
      }
      if (!Storage.readFileToString("WEB", path, 64 * 1024, b.scratch)) return {nullptr, 0};
      return span(b.scratch);
    },
    .decode = [](XpWebText text, uint8_t* out, size_t capacity) -> int32_t {
      return freeink::content::base64Decode(reinterpret_cast<const char*>(text.data), text.len, out, capacity);
    },
    .store = [](const char* path, const uint8_t* key, size_t len,
                int64_t expires) { return bookkey::write(path, key, len, expires); },
    .page = {requestHeader,
             sendAsset,
             {reinterpret_cast<const uint8_t*>(RunnerPageHtml), sizeof(RunnerPageHtml)},
             {reinterpret_cast<const uint8_t*>(RunnerPageHtmlETag), sizeof(RunnerPageHtmlETag) - 1}},
    .diagnostic =
        [](void*, uint32_t event, XpWebText a, XpWebText b) {
          switch (static_cast<XpWebPluginDiagnostic>(event)) {
            case XP_WEB_PLUGIN_REJECTED:
              LOG_ERR("WEB", "Rejected plugin file write: plugin='%.*s' path='%.*s'", int(a.len),
                      reinterpret_cast<const char*>(a.data), int(b.len), reinterpret_cast<const char*>(b.data));
              break;
            case XP_WEB_PLUGIN_SERVED:
              LOG_DBG("WEB", "Served plugin runner page");
              break;
            case XP_WEB_PLUGIN_JSON_OOM:
              LOG_ERR("WEB", "OOM: JSON response");
              break;
          }
        },
};

void CrossPointWebServer::handlePluginList() const { WebPluginBridge::route(*this, XP_WEB_PLUGIN_LIST); }
void CrossPointWebServer::handlePluginFile() const { WebPluginBridge::route(*this, XP_WEB_PLUGIN_FILE); }
void CrossPointWebServer::handleBookKey() { WebPluginBridge::route(*this, XP_WEB_PLUGIN_KEY); }
void CrossPointWebServer::handlePluginRunnerPage() const { WebPluginBridge::route(*this, XP_WEB_PLUGIN_PAGE); }
void CrossPointWebServer::handlePluginFs() { WebPluginBridge::upload(*this, 4); }
void CrossPointWebServer::handlePluginFsUpload() { WebPluginBridge::upload(*this, server->upload().status); }

struct WebRpcBridge : WebFileBridge {
  explicit WebRpcBridge(const CrossPointWebServer& owner) : WebFileBridge(owner) {}
  static const XpWebRpc request;
};

const XpWebRpc WebRpcBridge::request = {
    .files = &WebFileBridge::services,
    .parse = parseJson,
    .string = jsonString,
    .send = sendJsonLiteral,
    .release =
        [](void* ctx, bool arguments) {
          auto& b = self(ctx);
          b.paths.clear();
          if (arguments) {
            b.paths.shrinkToFit();
            b.argument = String();
            releaseRequestArguments(b.owner.server.get());
          }
        },
};

struct WebRelayBridge : WebRpcBridge {
  explicit WebRelayBridge(CrossPointWebServer& owner) : WebRpcBridge(owner), mutableOwner(owner) {}
  CrossPointWebServer& mutableOwner;
  std::string url, method, body;
  pluginhttp::Headers headers, responseHeaders;
  String responseBody;
  JsonDocument response;
  static WebRelayBridge& relay(void* ctx) { return static_cast<WebRelayBridge&>(self(ctx)); }
  static const XpWebRelay services;
};
const XpWebRelay WebRelayBridge::services = {
    .request = &WebRpcBridge::request,
    .prepare =
        [](void* ctx, XpWebText url, XpWebText method, XpWebText body) {
          auto& b = relay(ctx);
          b.url.assign(reinterpret_cast<const char*>(url.data), url.len);
          b.method.assign(reinterpret_cast<const char*>(method.data), method.len);
          pluginhttp::readHeaders(b.paths["headers"], b.headers);
          b.body.assign(reinterpret_cast<const char*>(body.data), body.len);
        },
    .suspend =
        [](void* ctx, bool suspend) {
          auto& b = relay(ctx);
          if (suspend) {
            b.mutableOwner.suspendTransferServices();
          } else {
            // Reclaim upstream response storage before restarting network services.
            b.response.clear();
            b.response.shrinkToFit();
            b.responseBody = String();
            pluginhttp::Headers{}.swap(b.responseHeaders);
            b.mutableOwner.resumeTransferServices();
          }
        },
    .request_tls = [](void* ctx, size_t limit) -> int32_t {
      auto& b = relay(ctx);
      LOG_DBG("WEB", "Relay TLS start: heap %u, max block %u: %s", (unsigned)ESP.getFreeHeap(),
              (unsigned)ESP.getMaxAllocHeap(), b.url.c_str());
      return pluginhttp::request(nullptr, b.url, b.method, b.body, b.headers, b.responseBody, limit, &b.responseHeaders,
                                 [] {
                                   resetTaskWatchdogIfSubscribed();
                                   return false;
                                 });
    },
    .header_count = [](void* ctx) -> size_t {
      auto& b = relay(ctx);
      b.response.to<JsonArray>();
      return b.responseHeaders.size();
    },
    .header = [](void* ctx, size_t index) -> XpWebHeader {
      const auto& h = relay(ctx).responseHeaders[index];
      return {span(h.first), span(h.second)};
    },
    .append_header =
        [](void* ctx, XpWebHeader header) {
          JsonArray pair = relay(ctx).response.as<JsonArray>().add<JsonArray>();
          pair.add(JsonString(reinterpret_cast<const char*>(header.name.data), header.name.len));
          pair.add(JsonString(reinterpret_cast<const char*>(header.value.data), header.value.len));
        },
    .respond =
        [](void* ctx, int32_t upstreamStatus, const char* statusHeader, const char* headersHeader, uint16_t status,
           const char* contentType) {
          auto& b = relay(ctx);
          String headersJson;
          serializeJson(b.response, headersJson);
          b.owner.server->sendHeader(statusHeader, String(upstreamStatus));
          b.owner.server->sendHeader(headersHeader, headersJson);
          b.owner.server->send(status, contentType, b.responseBody);
        },
};

struct WebCryptoBridge : WebRpcBridge {
  explicit WebCryptoBridge(CrossPointWebServer& owner) : WebRpcBridge(owner) {}
  std::optional<freeink::content::WolfsslCrypto> crypto;
  std::unique_ptr<uint8_t[]> buffers[4];
  std::vector<uint8_t> first, second;
  std::string password;
  JsonDocument response;
  static WebCryptoBridge& crypt(void* ctx) { return static_cast<WebCryptoBridge&>(self(ctx)); }
  static const XpWebCrypto services;
};
const XpWebCrypto WebCryptoBridge::services = {
    .request = &WebRpcBridge::request,
    .initialize = [](void* ctx) { crypt(ctx).crypto.emplace(); },
    .integer = [](void* ctx, const char* key, int32_t fallback) -> int32_t { return self(ctx).paths[key] | fallback; },
    .allocate = [](void* ctx, uint32_t slot, size_t n) -> uint8_t* {
      auto& buffer = crypt(ctx).buffers[slot];
      buffer = makeUniqueNoThrow<uint8_t[]>(n);
      return buffer.get();
    },
    .decode = [](XpWebText text, uint8_t* out, size_t capacity) -> int32_t {
      return freeink::content::base64Decode(reinterpret_cast<const char*>(text.data), text.len, out, capacity);
    },
    .call = [](void* ctx, uint32_t operation, XpWebText a, XpWebText b, XpWebText data, uint8_t* out,
               size_t capacity) -> ptrdiff_t {
      auto& bridge = crypt(ctx);
      auto& c = *bridge.crypto;
      switch (static_cast<XpWebCryptoOp>(operation)) {
        case XP_WEB_RANDOM:
          c.randomBytes(out, capacity);
          return 1;
        case XP_WEB_SHA1:
          c.sha1(data.data, data.len, out);
          return 1;
        case XP_WEB_SHA256:
          c.sha256(data.data, data.len, out);
          return 1;
        case XP_WEB_AES_ENCRYPT: {
          auto aes = makeUniqueNoThrow<Aes>();
          if (!out || !aes) return -2;
          return wc_AesSetKey(aes.get(), a.data, 16, b.data, AES_ENCRYPTION) == 0 &&
                         wc_AesCbcEncrypt(aes.get(), out, out, capacity) == 0
                     ? 1
                     : -1;
        }
        case XP_WEB_AES_DECRYPT:
          return c.aes128CbcDecrypt(a.data, b.data, data.data, data.len, out) ? 1 : -1;
        case XP_WEB_RSA_PRIVATE:
          return c.rsaPrivateRaw(a.data, a.len, data.data, data.len, out, capacity);
        case XP_WEB_RSA_GENERATE: {
          freeink::content::RsaKeyPairDer key;
          const bool ok = c.rsaGenerate(&key);
          bridge.first = std::move(key.spki);
          bridge.second = std::move(key.pkcs8);
          return ok ? 1 : -1;
        }
        case XP_WEB_RSA_PUBLIC: {
          size_t size = 0;
          return c.rsaPublicEncrypt(a.data, a.len, data.data, data.len, out, capacity, &size)
                     ? static_cast<ptrdiff_t>(size)
                     : -1;
        }
        case XP_WEB_RSA_SIGN:
          return c.rsaPrivateSignRaw(a.data, a.len, data.data, out) ? 1 : -1;
        case XP_WEB_PKCS12:
          return c.pkcs12Extract(data.data, data.len, bridge.password, &bridge.first, &bridge.second) ? 1 : -1;
      }
      return -1;
    },
    .output = [](void* ctx, uint32_t index) -> XpWebText {
      const auto& bytes = index == 0 ? crypt(ctx).first : crypt(ctx).second;
      return {bytes.data(), bytes.size()};
    },
    .last_error = [](void* ctx) -> XpWebText { return span(crypt(ctx).crypto->lastError); },
    .password = [](void* ctx,
                   XpWebText text) { crypt(ctx).password.assign(reinterpret_cast<const char*>(text.data), text.len); },
    .encode = [](void* ctx, const char* key, XpWebText bytes) -> bool {
      const uint8_t empty = 0;
      const String encoded = base64::encode(bytes.len ? bytes.data : &empty, bytes.len);
      if ((bytes.len && encoded.isEmpty()) || encoded == "-FAIL-") return false;
      crypt(ctx).response[key] = encoded;
      return true;
    },
    .error =
        [](void* ctx, const XpWebText* parts, size_t count, bool linked) {
          auto& response = crypt(ctx).response;
          if (linked) {
            response["error"] = JsonString(reinterpret_cast<const char*>(parts[0].data), parts[0].len, true);
            return;
          }
          size_t size = 0;
          for (size_t i = 0; i < count; ++i) size += parts[i].len;
          std::string text;
          text.reserve(size);
          for (size_t i = 0; i < count; ++i) text.append(reinterpret_cast<const char*>(parts[i].data), parts[i].len);
          response["error"] = text;
        },
    .clear =
        [](void* ctx) {
          auto& b = crypt(ctx);
          for (auto& buffer : b.buffers) buffer.reset();
          std::vector<uint8_t>{}.swap(b.first);
          std::vector<uint8_t>{}.swap(b.second);
          std::string{}.swap(b.password);
        },
    .overflowed = [](void* ctx) { return crypt(ctx).response.overflowed(); },
    .send_response = [](void* ctx) { crypt(ctx).owner.sendJson(crypt(ctx).response); },
    .oom = [](void*, const char* operation) { LOG_ERR("WEB", "OOM: crypto %s", operation); },
};

void CrossPointWebServer::handleRelay() {
  WebRelayBridge bridge(*this);
  xp_web_relay(static_cast<WebFileBridge*>(&bridge), &WebRelayBridge::services);
}
void CrossPointWebServer::handleCrypto() {
  WebCryptoBridge bridge(*this);
  xp_web_crypto(static_cast<WebFileBridge*>(&bridge), &WebCryptoBridge::services);
}

// POST /api/fetch {plugin, url, dest, headers?, offset?, maxBytes?}
//   -> {status, bytes, complete, total?}
// Device downloads a URL straight to SD, so a large body never passes through
// the browser.
void CrossPointWebServer::handleFetch() {
  if (rejectIfPluginsDisabled()) return;
  JsonDocument req;
  if (!readJsonBody(req)) return;
  const std::string url = req["url"] | "";
  const std::string dest = req["dest"] | "";
  const size_t requestedOffset = req["offset"] | 0;
  size_t segmentLimit = req["maxBytes"] | 0;
  static constexpr size_t FETCH_MAX_SEGMENT_SIZE = 4 * 1024 * 1024;
  if (segmentLimit > FETCH_MAX_SEGMENT_SIZE) segmentLimit = FETCH_MAX_SEGMENT_SIZE;
  if (url.empty() || !protectedpaths::isPluginPath(dest)) {
    server->send(400, "application/json", "{\"error\":\"bad url/dest\"}");
    return;
  }

  pluginhttp::Headers requestHeaders;
  pluginhttp::readHeaders(req["headers"], requestHeaders);
  req.clear();
  req.shrinkToFit();
  releaseRequestArguments(server.get());

  // Stage in <dest>.part so an interrupted or abandoned download never sits
  // under the real name, and an existing dest survives until the new copy is complete.
  const std::string part = dest + ".part";
  HalFile file;
  if (requestedOffset == 0) {
    // Mirror handlePluginFs(): create missing parents so a plugin's first fetch
    // into a fresh subfolder (e.g. /.crosspoint/plugins/<name>/) doesn't fail
    // before anything has a chance to create it.
    const size_t lastSlash = dest.rfind('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
      Storage.ensureDirectoryExists(dest.substr(0, lastSlash).c_str());
    }
    Storage.remove(part.c_str());
    if (!Storage.openFileForWrite("PLG", part, file)) {
      server->send(500, "application/json", "{\"error\":\"cannot create file\"}");
      return;
    }
  } else {
    file = Storage.open(part.c_str(), O_RDWR | O_AT_END);
    const size_t existingSize = file ? file.size() : 0;
    if (!file || existingSize != requestedOffset) {
      if (file) file.close();
      char msg[96];
      snprintf(msg, sizeof(msg), "{\"error\":\"offset mismatch\",\"bytes\":%u}", (unsigned)existingSize);
      server->send(409, "application/json", msg);
      return;
    }
  }

  // Resume and Range-restart handling live in fetchResumable (ResumableFetch.h).
  suspendTransferServices();
  ScopedCleanup resumeServices{[this] { resumeTransferServices(); }};
  WifiPowerSaveGuard psGuard;

  size_t written = requestedOffset;
  size_t nextHeapLog = written;
  bool sdFull = false;
  bool segmentBoundary = false;
  bool rangeUnsupported = false;
  const unsigned long fetchStartedAt = millis();
  unsigned long lastBrowserHeartbeat = fetchStartedAt;
  bool browserResponseStarted = false;

  // A phone may discard an HTTP response that sends no bytes for several
  // minutes even while the device is actively downloading upstream. Start a
  // chunked JSON response only once the operation becomes long-running, then
  // send JSON whitespace to keep that browser-facing connection active.
  const auto keepBrowserAlive = [this, &browserResponseStarted, &lastBrowserHeartbeat]() {
    const unsigned long now = millis();
    if (now - lastBrowserHeartbeat < 5000) return;
    lastBrowserHeartbeat = now;
    if (!server->client().connected()) return;
    if (!browserResponseStarted) {
      server->setContentLength(CONTENT_LENGTH_UNKNOWN);
      server->send(200, "application/json", "");
      browserResponseStarted = true;
    }
    server->sendContent(" \n", 2);
  };
  const auto sendFetchResult = [this, &browserResponseStarted](int code, const String& payload) {
    if (!browserResponseStarted) {
      server->send(code, "application/json", payload);
      return;
    }
    if (server->client().connected()) {
      server->sendContent(payload);
      server->sendContent("", 0);
    }
  };

  freeink::FetchOptions options;
  options.startOffset = requestedOffset;
  freeink::FetchSink sink;
  sink.write = [&](const uint8_t* data, size_t len) {
    resetTaskWatchdogIfSubscribed();
    const size_t writeLen = segmentLimit > 0 ? std::min(len, requestedOffset + segmentLimit - written) : len;
    if (file.write(data, writeLen) != writeLen) {
      sdFull = true;
      return false;
    }
    written += writeLen;
    // Heap trajectory during the transfer: a steady value rules RAM out of a
    // mid-body failure; a falling one implicates it.
    if (written >= nextHeapLog) {
      LOG_DBG("WEB", "Fetch %u bytes, heap %u", (unsigned)written, (unsigned)ESP.getFreeHeap());
      nextHeapLog = written + 1024 * 1024;
    }
    keepBrowserAlive();
    // A bounded segment stops here; the next browser request resumes from
    // `written` with Range.
    if (segmentLimit > 0 && written - requestedOffset >= segmentLimit) {
      segmentBoundary = true;
      return false;
    }
    return true;
  };
  sink.rewind = [&] {
    // Range ignored: the body restarts from byte 0, which only a transfer
    // that has not yet reported progress to the browser can follow.
    if (requestedOffset > 0) {
      rangeUnsupported = true;
      return false;
    }
    file.close();
    written = 0;
    return Storage.openFileForWrite("PLG", part, file);
  };
  const freeink::FetchResult result = freeink::fetchResumable(
      url, options,
      [&](freeink::SecureHttpClient& http, const bool sameOrigin) {
        http.setUserAgent("CrossPoint");
        // The SecureNet transport ships no CA bundle, so peer verification always
        // fails (wolfSSL -188); skip it like HttpDownloader does. Traffic stays
        // TLS-encrypted, just unauthenticated — matching the prior library-lending flow.
        http.setInsecure();
        // Some delivery servers assemble books on the fly and can stall mid-body
        // while packaging; the default 15s no-data timeout truncates those downloads.
        http.setTimeout(60000);
        // The plugin's headers (typically its Authorization) stay with the
        // starting origin; a redirect to another server gets none of them.
        if (sameOrigin) {
          for (const auto& header : requestHeaders) http.addHeader(header.first, header.second);
        }
      },
      sink,
      // The write callback only runs when bytes arrive; with the 60s no-data
      // timeout a server stall would starve this task's WDT subscription.
      // shouldAbort is polled in every wait loop.
      [&] {
        resetTaskWatchdogIfSubscribed();
        keepBrowserAlive();
        return false;  // never aborts; only feeds
      });
  if (file.isOpen()) {
    file.flush();
    file.close();
  }
  const int status = result.status;
  const size_t totalExpected = result.total;
  bool complete = result.complete || (segmentBoundary && totalExpected > 0 && written >= totalExpected);

  const bool ok2xx = status >= 200 && status < 300;
  // A bounded segment ended mid-body: the browser requests the next one, so
  // the .part stays and nothing is installed yet.
  const bool midSegment = segmentBoundary && !complete && ok2xx;

  if (!complete && ok2xx && !midSegment) {
    Storage.remove(part.c_str());
    char msg[96];
    const char* error = sdFull ? "sd write failed" : rangeUnsupported ? "range unsupported" : "download truncated";
    // complete:false matters once the heartbeat has committed HTTP 200 chunked:
    // it is the only signal fetchToSd()'s resume loop still sees on this path
    // (it then detects zero progress and throws instead of returning success).
    snprintf(msg, sizeof(msg), "{\"error\":\"%s\",\"bytes\":%u,\"complete\":false}", error, (unsigned)written);
    LOG_ERR("WEB", "Fetch failed after %u bytes in %lu ms: %s", (unsigned)written, millis() - fetchStartedAt,
            url.c_str());
    sendFetchResult(502, msg);
    return;
  }

  JsonDocument resp;
  if (!ok2xx) {
    Storage.remove(part.c_str());
    resp["error"] = status < 0 ? "transport failure" : "http status";
  } else if (complete && !Storage.replaceFile(part.c_str(), dest.c_str())) {
    Storage.remove(part.c_str());
    complete = false;
    resp["error"] = "sd write failed";
  } else if (complete && library::isLibraryBookFile(dest)) {
    library::markLibraryIndexDirty();
  }
  resp["status"] = status;
  resp["bytes"] = written;
  resp["complete"] = complete;
  if (totalExpected > 0) resp["total"] = totalExpected;
  String out;
  serializeJson(resp, out);
  const bool browserConnected = server->client().connected();
  LOG_INF("WEB", "Fetch %s: %u bytes in %lu ms, browser %s: %s",
          midSegment ? "segment done"
          : complete ? "complete"
                     : "failed",
          (unsigned)written, millis() - fetchStartedAt, browserConnected ? "connected" : "disconnected", url.c_str());
  sendFetchResult(200, out);
}

struct WebJobBridge : WebFileBridge {
  static void route(CrossPointWebServer& owner, XpWebJobRoute route) {
    WebFileBridge bridge(owner);
    xp_web_job_route(&bridge, &services, &owner.pluginJobs, route);
  }
  static const XpWebJobServices services;
};

const XpWebJobServices WebJobBridge::services = {
    .files = &WebFileBridge::services,
    .parse = parseJson,
    .string = jsonString,
    .integer = [](void* ctx, const char* key) -> uint32_t { return self(ctx).paths[key] | 0; },
    .boolean = [](void* ctx, const char* key) -> bool { return self(ctx).paths[key] | false; },
    .json = [](void* ctx, const char* key, uint8_t* out, size_t capacity) -> size_t {
      const auto value = self(ctx).paths[key];
      if (value.isNull()) return 0;
      return out ? serializeJson(value, reinterpret_cast<char*>(out), capacity) : measureJson(value);
    },
    .time = WebFileBridge::transfers.time,
    .send = sendJsonLiteral,
    // Queued/expired/stale/completed map to the original WEB milestones.
    .diagnostic =
        [](void*, uint32_t event, uint32_t id, XpWebText plugin, XpWebText action) {
          switch (static_cast<XpWebJobDiagnostic>(event)) {
            case XP_WEB_JOB_QUEUED:
              LOG_INF("WEB", "Plugin job %u queued: %.*s/%.*s", (unsigned)id, static_cast<int>(plugin.len),
                      reinterpret_cast<const char*>(plugin.data), static_cast<int>(action.len),
                      reinterpret_cast<const char*>(action.data));
              break;
            case XP_WEB_JOB_EXPIRED:
              LOG_INF("WEB", "Plugin job %u lease expired; requeued", (unsigned)id);
              break;
            case XP_WEB_JOB_STALE:
              LOG_INF("WEB", "Plugin job %u: stale completion ignored", (unsigned)id);
              break;
            case XP_WEB_JOB_DONE:
            case XP_WEB_JOB_FAILED:
              LOG_INF("WEB", "Plugin job %u %s", (unsigned)id, event == XP_WEB_JOB_DONE ? "done" : "failed");
              break;
          }
        }};

void CrossPointWebServer::handlePluginJobSubmit() { WebJobBridge::route(*this, XP_WEB_JOB_SUBMIT); }
void CrossPointWebServer::handlePluginJobClaim() { WebJobBridge::route(*this, XP_WEB_JOB_CLAIM); }
void CrossPointWebServer::handlePluginJobComplete() { WebJobBridge::route(*this, XP_WEB_JOB_COMPLETE); }
void CrossPointWebServer::handlePluginJobStatus() { WebJobBridge::route(*this, XP_WEB_JOB_STATUS); }

// WebSocket callback trampoline
void CrossPointWebServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}

// WebSocket event handler for fast binary uploads
// Protocol:
//   1. Client sends TEXT message: "START:<filename>:<size>:<path>"
//   2. Client sends BINARY messages with file data chunks
//   3. Server sends TEXT "PROGRESS:<received>:<total>" after each chunk
//   4. Server sends TEXT "DONE" or "ERROR:<message>" when complete
void CrossPointWebServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      if (wsSending) {
        wsSendDisconnected = true;
        break;
      }
      WebFileBridge::ws(*this, num, 0, payload, length);
      break;
    case WStype_CONNECTED:
      WebFileBridge::ws(*this, num, 1, payload, length);
      break;
    case WStype_TEXT:
      WebFileBridge::ws(*this, num, 2, payload, length);
      break;
    case WStype_BIN:
      WebFileBridge::ws(*this, num, 3, payload, length);
      break;
    default:
      break;
  }
}

// --- Font management handlers ---

struct WebFontBridge : WebFileBridge {
  explicit WebFontBridge(const CrossPointWebServer& owner) : WebFileBridge(owner) {}
  std::vector<uint8_t> sizes;
  char path[SdCardFontFamilyInfo::PATH_BUFFER_SIZE];
  static WebFontBridge& fonts(void* ctx) { return static_cast<WebFontBridge&>(self(ctx)); }
  static void route(const CrossPointWebServer& owner, XpWebFontRoute route) {
    WebFontBridge bridge(owner);
    xp_web_fonts(static_cast<WebFileBridge*>(&bridge), &services, route);
  }
  static void upload(CrossPointWebServer& owner, uint32_t event) {
    static_assert(CrossPointWebServer::FontUploadState::BUFFER_SIZE == XP_WEB_BUFFER_CAPACITY);
    WebFileBridge bridge(owner);
    auto& s = owner.fontUpload;
    bridge.fontState = &s;
    bridge.borrowed = &s.file;
    XpWebFontUpload state{s.bytesWritten, s.bufferPos, s.buffer.get(), s.valid, s.magicChecked, s.isVector};
    if (event == 4) {
      xp_web_font_upload(&bridge, &services, &state, event, {nullptr, 0}, nullptr, 0);
    } else {
      const auto& part = owner.server->upload();
      xp_web_font_upload(&bridge, &services, &state, event, span(part.filename), part.buf, part.currentSize);
    }
    s.bytesWritten = state.bytes;
    s.bufferPos = state.buffer_pos;
    s.valid = state.valid;
    s.magicChecked = state.magic_checked;
    s.isVector = state.vector;
  }
  static const XpWebFonts services;
};

const XpWebFonts WebFontBridge::services = {
    .transfers = &WebFileBridge::transfers,
    .parse = parseJson,
    .json_string = [](void* ctx, const char* key) -> XpWebText {
      const char* value = self(ctx).paths[key].as<const char*>();
      return {reinterpret_cast<const uint8_t*>(value), value ? strlen(value) : 0};
    },
    .registry = [](void*, uint32_t op) -> size_t {
      switch (static_cast<XpWebFontRegistryOp>(op)) {
        case XP_WEB_FONT_REFRESH:
          sdFontSystem.refreshIfDirty();
          return sdFontSystem.registry().getFamilies().size();
        case XP_WEB_FONT_MAX:
          return SdCardFontRegistry::MAX_SD_FAMILIES;
        case XP_WEB_FONT_PSRAM:
          return HalMemory::getPsramHeap().totalBytes > 0;
        case XP_WEB_FONT_DIRTY:
          sdFontSystem.markRegistryDirty();
          return 0;
      }
      return 0;
    },
    .family =
        [](void* ctx, size_t index, XpWebFontFamily* out) {
          auto& b = fonts(ctx);
          const auto& family = sdFontSystem.registry().getFamilies()[index];
          b.sizes = family.availableSizes();
          *out = {span(family.name), {b.sizes.data(), b.sizes.size()}, family.files.size()};
        },
    .file = [](void* ctx, size_t familyIndex, size_t index) -> XpWebText {
      auto& b = fonts(ctx);
      const auto& family = sdFontSystem.registry().getFamilies()[familyIndex];
      if (!family.filePath(family.files[index], b.path, sizeof(b.path))) return {nullptr, 0};
      return {reinterpret_cast<const uint8_t*>(b.path), strlen(b.path)};
    },
    .install =
        [](void*, uint32_t op, const char* family, const char* filename, uint8_t* out, size_t capacity) {
          FontInstaller installer(sdFontSystem.registry());
          switch (static_cast<XpWebFontInstallOp>(op)) {
            case XP_WEB_FONT_VALID_FAMILY:
              return FontInstaller::isValidFamilyName(family);
            case XP_WEB_FONT_VALID_CPFONT:
              return FontInstaller::isValidCpfontFilename(filename);
            case XP_WEB_FONT_VALID_VECTOR:
              return FontInstaller::isValidVectorFontFilename(filename);
            case XP_WEB_FONT_ENSURE_DIR:
              return installer.ensureFamilyDir(family);
            case XP_WEB_FONT_BUILD_PATH:
              FontInstaller::buildFontPath(family, filename, reinterpret_cast<char*>(out), capacity);
              return true;
            case XP_WEB_FONT_REMOVE_FAMILY:
              return installer.deleteFamily(family) == FontInstaller::Error::OK;
          }
          return false;
        },
    .page = {requestHeader,
             sendAsset,
             {reinterpret_cast<const uint8_t*>(FontsPageHtml), sizeof(FontsPageHtml)},
             {reinterpret_cast<const uint8_t*>(FontsPageHtmlETag), std::char_traits<char>::length(FontsPageHtmlETag)}},
    .diagnostic =
        [](void*, uint32_t event, XpWebText a, XpWebText b, size_t bytes, bool valid) {
          const char* x = a.len ? reinterpret_cast<const char*>(a.data) : "";
          const char* y = b.len ? reinterpret_cast<const char*>(b.data) : "";
          switch (static_cast<XpWebFontDiagnostic>(event)) {
            case XP_WEB_FONT_SERVED:
              LOG_DBG("WEB", "Served fonts page");
              break;
            case XP_WEB_FONT_BAD_FAMILY:
              LOG_ERR("WEB", "Invalid font family name: %.*s", int(a.len), x);
              break;
            case XP_WEB_FONT_BAD_NAME:
              LOG_ERR("WEB", "Invalid font filename: %.*s", int(a.len), x);
              break;
            case XP_WEB_FONT_DIR_FAILED:
              LOG_ERR("WEB", "Failed to create font family dir");
              break;
            case XP_WEB_FONT_OOM:
              LOG_ERR("WEB", "OOM: font upload buffer");
              break;
            case XP_WEB_FONT_OPEN_FAILED:
              LOG_ERR("WEB", "Failed to open font file for write: %.*s", int(a.len), x);
              break;
            case XP_WEB_FONT_STARTED:
              LOG_DBG("WEB", "Font upload started: %.*s -> %.*s", int(a.len), x, int(b.len), y);
              break;
            case XP_WEB_FONT_BAD_MAGIC:
              LOG_ERR("WEB", "Invalid font magic bytes");
              break;
            case XP_WEB_FONT_WRITE_FAILED:
              LOG_ERR("WEB", "Font upload write failed: %.*s", int(a.len), x);
              break;
            case XP_WEB_FONT_ENDED:
              LOG_DBG("WEB", "Font upload end: valid=%d, %zu bytes", valid, bytes);
              break;
            case XP_WEB_FONT_ABORTED:
              LOG_DBG("WEB", "Font upload aborted");
              break;
            case XP_WEB_FONT_COMPLETE:
              LOG_DBG("WEB", "Font upload complete: %.*s", int(a.len), x);
              break;
            case XP_WEB_FONT_DELETED:
              LOG_DBG("WEB", "Deleted font family: %.*s", int(a.len), x);
              break;
            case XP_WEB_FONT_DELETE_FAILED:
              LOG_ERR("WEB", "Failed to delete font family: %.*s", int(a.len), x);
              break;
            case XP_WEB_FONT_JSON_OOM:
              LOG_ERR("WEB", "OOM: JSON response");
              break;
          }
        }};

void CrossPointWebServer::handleFontsPage() const { WebFontBridge::route(*this, XP_WEB_FONT_PAGE); }
void CrossPointWebServer::handleFontList() const { WebFontBridge::route(*this, XP_WEB_FONT_LIST); }
void CrossPointWebServer::handleFontDelete() { WebFontBridge::route(*this, XP_WEB_FONT_DELETE); }
void CrossPointWebServer::handleFontUpload() { WebFontBridge::upload(*this, 4); }
void CrossPointWebServer::handleFontUploadData() { WebFontBridge::upload(*this, server->upload().status); }
