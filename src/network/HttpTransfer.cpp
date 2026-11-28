#include "HttpTransfer.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <esp_wifi.h>

#include <cstring>
#include <memory>
#include <new>

namespace httptransfer {
namespace {
const std::string& string(const XpHttpNativeText& value) {
  return *static_cast<const std::string*>(value.native_string);
}
freeink::SecureHttpClient& client(void* storage) { return *static_cast<freeink::SecureHttpClient*>(storage); }
// cppcheck-suppress passedByValueCallback // Rust's C ABI passes XpHttpNativeText by value.
bool acquire(void* storage, XpHttpNativeText url) {
  auto* http = new (storage) freeink::SecureHttpClient;
  return http->begin(string(url));
}
void release(void* storage) { std::destroy_at(static_cast<freeink::SecureHttpClient*>(storage)); }
void configure(void* storage, const XpHttpSdkConfiguration* configuration) {
  auto& http = client(storage);
  http.setTimeout(configuration->timeout_ms);
  if (configuration->insecure) http.setInsecure();
  // Rust selects the agent prefix; this translation unit supplies the build identity.
  {
    constexpr char version[] = CROSSPOINT_VERSION;
    std::string agent(configuration->user_agent.length + sizeof(version) - 1, '\0');
    std::memcpy(agent.data(), configuration->user_agent.data, configuration->user_agent.length);
    std::memcpy(agent.data() + configuration->user_agent.length, version, sizeof(version) - 1);
    http.setUserAgent(agent);
  }
  if (configuration->basic_auth) http.setBasicAuth(string(configuration->username), string(configuration->password));
}
// cppcheck-suppress passedByValueCallback // Rust's C ABI passes both text descriptors by value.
void addHeader(void* storage, XpHttpNativeText name, XpHttpNativeText value) {
  client(storage).addHeader(string(name), string(value));
}
XpHttpResponseFacts facts(const freeink::SecureHttpClient& http) {
  return {http.getStatus(), http.getContentLength(), http.hasContentLength(), http.responseComplete(), http.aborted()};
}
int32_t send(void* storage, const char* method, XpHttpBytes body, const XpHttpAttemptCallbacks* callbacks,
             XpHttpResponseFacts* output) {
  auto& http = client(storage);
  // Two pointers fit the target std::function inline storage. No owner is copied.
  const auto onChunk = [&http, callbacks](const uint8_t* bytes, size_t length) {
    const auto response = facts(http);
    return callbacks->chunk(callbacks->context, {bytes, length}, &response);
  };
  freeink::SecureHttpClient::AbortCallback shouldAbort;
  if (callbacks->should_abort) {
    shouldAbort = [callbacks] { return callbacks->should_abort(callbacks->context); };
  }
  const int status = http.sendRequest(method, body.data, body.length, onChunk, shouldAbort);
  *output = facts(http);
  return status;
}
void responseHeader(void* storage, const char* name, XpHttpTextStorageRetireP19* text, XpHttpMutableText* output) {
  auto* value = new (text) std::string(client(storage).getHeader(name));
  *output = mutableText(*value);
}
// cppcheck-suppress passedByValueCallback // Rust's C ABI passes the path descriptor by value.
bool openWrite(void* file, XpHttpNativeText path) {
  return Storage.openFileForWrite("HTTP", string(path).c_str(), *static_cast<HalFile*>(file));
}
size_t write(void* file, XpHttpBytes bytes) { return static_cast<HalFile*>(file)->write(bytes.data, bytes.length); }
void close(void* storage, bool releaseHandle) {
  auto& file = *static_cast<HalFile*>(storage);
  if (releaseHandle)
    file = HalFile{};
  else if (file.isOpen())
    file.close();
}
// cppcheck-suppress passedByValueCallback // Rust's C ABI passes the path descriptor by value.
bool remove(XpHttpNativeText path) { return Storage.remove(string(path).c_str()); }
// cppcheck-suppress passedByValueCallback // Rust's C ABI passes both path descriptors by value.
bool replace(XpHttpNativeText from, XpHttpNativeText to) {
  return Storage.replaceFile(string(from).c_str(), string(to).c_str());
}
int32_t getPowerSave(int32_t* mode) {
  wifi_ps_type_t value;
  const auto error = esp_wifi_get_ps(&value);
  if (error == ESP_OK) *mode = value;
  return error;
}
int32_t setPowerSave(int32_t mode) { return esp_wifi_set_ps(static_cast<wifi_ps_type_t>(mode)); }
void diagnostic(XpHttpDiagnostic event, XpHttpBytes url, XpHttpBytes destination, size_t a, size_t b) {
  const auto* source = reinterpret_cast<const char*>(url.data);
  const auto* path = reinterpret_cast<const char*>(destination.data);
  switch (event) {
    case XP_HTTP_LOG_FETCHING:
      LOG_DBG("HTTP", "Fetching: %s", source);
      break;
    case XP_HTTP_LOG_DOWNLOADING:
      LOG_DBG("HTTP", "Downloading: %s -> %s", source, path);
      break;
    case XP_HTTP_LOG_POST_DOWNLOAD:
      LOG_DBG("HTTP", "POST download: %s -> %s", source, path);
      break;
    case XP_HTTP_LOG_GET_HEAP:
      LOG_DBG("HTTP", "wolfSSL GET: %s (heap %u, max block %u)", source, (unsigned)ESP.getFreeHeap(),
              (unsigned)ESP.getMaxAllocHeap());
      break;
    case XP_HTTP_LOG_GET:
      LOG_DBG("HTTP", "wolfSSL GET: %s", source);
      break;
    case XP_HTTP_LOG_POST:
      LOG_DBG("HTTP", "wolfSSL POST: %s", source);
      break;
    case XP_HTTP_LOG_BAD_URL:
      LOG_ERR("HTTP", "bad URL: %s", source);
      break;
    case XP_HTTP_LOG_UNAUTHORIZED:
      LOG_ERR("HTTP", "wolfSSL request unauthorized: status %d: %s", (int)a, source);
      break;
    case XP_HTTP_LOG_FAILED:
      LOG_ERR("HTTP", "wolfSSL request failed: status %d: %s", (int)a, source);
      break;
    case XP_HTTP_LOG_INCOMPLETE:
      LOG_ERR("HTTP", "wolfSSL incomplete: got %zu of %zu bytes", a, b);
      break;
    case XP_HTTP_LOG_OPEN_FAILED:
      LOG_ERR("HTTP", "Failed to open file for writing");
      break;
    case XP_HTTP_LOG_POST_FAILED:
      LOG_ERR("HTTP", "POST failed: status %d: %s", (int)a, source);
      break;
    case XP_HTTP_LOG_EMPTY:
      LOG_ERR("HTTP", "no data received");
      break;
    case XP_HTTP_LOG_POST_INCOMPLETE:
      LOG_ERR("HTTP", "POST response incomplete: %zu of %zu bytes", a, b);
      break;
    case XP_HTTP_LOG_REPLACE_FAILED:
      LOG_ERR("HTTP", "Failed to move download into place: %s", path);
      break;
    case XP_HTTP_LOG_DOWNLOADED:
      LOG_DBG("HTTP", "Downloaded %zu bytes", a);
      break;
    case XP_HTTP_LOG_GUARD_ENTER_FAILED:
      LOG_ERR("WIFI", "Failed to disable WiFi power-save: %d", (int)a);
      break;
    case XP_HTTP_LOG_GUARD_RESTORE_FAILED:
      LOG_ERR("WIFI", "Failed to restore WiFi power-save: %d", (int)a);
      break;
  }
}
constexpr XpHttpSdkLeaves sdkLeaves{acquire, release, configure, addHeader, send, responseHeader};
constexpr XpHttpFileLeaves fileLeaves{openWrite, write, close, remove, replace};
constexpr XpHttpRadioLeaves radioLeaves{getPowerSave, setPowerSave};
constexpr XpHttpDiagnosticsRetireZ1 diagnostics{diagnostic};
constexpr XpHttpLeaves physicalLeaves{&sdkLeaves, &fileLeaves, &radioLeaves, &textLeaves, &diagnostics};
}  // namespace
const XpHttpLeaves& leaves() { return physicalLeaves; }
}  // namespace httptransfer
