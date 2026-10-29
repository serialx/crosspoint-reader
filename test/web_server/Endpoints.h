#pragma once

#define PROGMEM
#define CROSSPOINT_VERSION "oracle"
#define FREEINK_DEVICE_X3 1
#define FREEINK_DEVICE_X4 0
#define CONFIG_IDF_TARGET_ESP32 0
constexpr int O_RDONLY = 0, O_RDWR = 1, O_AT_END = 2;
constexpr int ESP_OK = 0, ESP_EFUSE_USER_DATA = 0;
inline int esp_efuse_read_field_blob(int, void* dst, size_t) {
  std::memcpy(dst, "TEST-SERIAL", 12);
  return host::fail("efuse") ? -1 : 0;
}
inline bool deviceSecret(uint8_t* dst) {
  std::memset(dst, 7, 32);
  return !host::fail("secret");
}
inline int wc_Sha256Hash(const uint8_t*, size_t, uint8_t* dst) {
  std::memset(dst, 8, 32);
  return host::fail("sha256") ? -1 : 0;
}
struct GPIO {
  bool deviceIsX3() const { return true; }
};
inline GPIO gpio;
namespace firmwareidentity {
constexpr const char* FAMILY = "crosspoint";
constexpr int PLUGIN_API_VERSION = 1;
constexpr const char* PLUGIN_CAPABILITIES = "oracle";
}  // namespace firmwareidentity
using wifi_mode_t = int;
constexpr int WIFI_MODE_STA = 1, WIFI_MODE_AP = 2, WL_CONNECTED = 3;
struct IP {
  String toString() const { return "192.0.2.1"; }
};
struct WifiStub {
  int mode = WIFI_MODE_STA, statusValue = WL_CONNECTED;
  int getMode() const { return mode; }
  int status() const { return statusValue; }
  int softAPgetStationNum() const { return 0; }
  IP softAPIP() const { return {}; }
  IP localIP() const { return {}; }
  int RSSI() const { return -42; }
  const char* getHostname() const { return "oracle"; }
  void setSleep(bool value) { host::trace.push_back("wifi-sleep:" + std::to_string(value)); }
  void setAutoReconnect(bool value) { host::trace.push_back("wifi-reconnect:" + std::to_string(value)); }
};
inline WifiStub WiFi;
struct WebDAVHandler {};
struct WifiPowerSaveGuard {
  WifiPowerSaveGuard() { host::trace.emplace_back("power-save-acquire"); }
  ~WifiPowerSaveGuard() { host::trace.emplace_back("power-save-release"); }
};
struct ScopedCleanup {
  std::function<void()> cleanup;
  ~ScopedCleanup() { cleanup(); }
};
enum class StrId { STR_STATE_OFF, STR_STATE_ON, NAME, CATEGORY };
struct I18nStub {
  const char* get(StrId id) const {
    return id == StrId::STR_STATE_OFF  ? "Off"
           : id == StrId::STR_STATE_ON ? "On"
           : id == StrId::NAME         ? "Name"
                                       : "Category";
  }
};
inline I18nStub I18N;
enum class SettingType { TOGGLE, ENUM, VALUE, STRING, ACTION };
struct SettingInfo {
  const char* key = nullptr;
  bool webHidden = false, obfuscated = false;
  StrId nameId = StrId::NAME, category = StrId::CATEGORY;
  SettingType type = SettingType::ACTION;
  uint8_t SettingsStub::* valuePtr = nullptr;
  std::function<uint8_t()> valueGetter;
  std::function<void(uint8_t)> valueSetter;
  std::function<std::string()> stringGetter;
  std::function<void(const std::string&)> stringSetter;
  std::vector<std::string> enumStringValues;
  std::vector<StrId> labels;
  const std::vector<StrId>& enumLabels() const { return labels; }
  struct {
    int min = 0, max = 10, step = 1;
  } valueRange;
  size_t stringOffset = 0, stringMaxLen = 0;
};
struct DictionaryEntry {};
namespace DictionaryRegistry {
inline void discover(std::vector<DictionaryEntry>&) {}
}  // namespace DictionaryRegistry
struct FontFile {
  std::string name = "font.cpfont";
};
struct SdCardFontFamilyInfo {
  static constexpr size_t PATH_BUFFER_SIZE = 128;
  std::string name = "Test";
  std::vector<FontFile> files = {{}};
  std::vector<uint8_t> availableSizes() const { return {12, 16}; }
  bool filePath(const FontFile& f, char* out, size_t cap) const {
    return snprintf(out, cap, "/fonts/%s/%s", name.c_str(), f.name.c_str()) < static_cast<int>(cap);
  }
};
struct SdCardFontRegistry {
  static constexpr size_t MAX_SD_FAMILIES = 8;
  std::vector<SdCardFontFamilyInfo> families = {{}};
  const auto& getFamilies() const { return families; }
};
struct SdCardFontSystem {
  SdCardFontRegistry fonts;
  SdCardFontRegistry& registry() { return fonts; }
  void markRegistryDirty() { host::trace.emplace_back("font-dirty"); }
  void refreshIfDirty() { host::trace.emplace_back("font-refresh"); }
};
inline SdCardFontSystem sdFontSystem;
namespace HalMemory {
inline bool psram = false;
struct Heap {
  size_t totalBytes;
};
inline Heap getPsramHeap() { return {psram ? 8000000u : 0u}; }
}  // namespace HalMemory
struct FontInstaller {
  explicit FontInstaller(SdCardFontRegistry&) {}
  enum class Error { OK, FAIL };
  static bool isValidFamilyName(const char* n) { return *n && !strpbrk(n, "/\\."); }
  static bool isValidCpfontFilename(const char* n) { return !strpbrk(n, "/\\") && std::string(n).ends_with(".cpfont"); }
  static bool isValidVectorFontFilename(const char* n) {
    return !strpbrk(n, "/\\") && (std::string(n).ends_with(".ttf") || std::string(n).ends_with(".otf"));
  }
  bool ensureFamilyDir(const char* family) {
    host::trace.push_back(std::string("font-dir:") + family);
    return !host::fail("font-dir");
  }
  static void buildFontPath(const char* family, const char* name, char* out, size_t n) {
    snprintf(out, n, "/fonts/%s/%s", family, name);
  }
  Error deleteFamily(const char* family) {
    host::trace.push_back(std::string("font-delete:") + family);
    return host::fail("font-delete") ? Error::FAIL : Error::OK;
  }
};
inline bool extendedSettings = false;
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry*, const std::vector<DictionaryEntry>*) {
  std::vector<SettingInfo> out;
  SettingInfo s;
  s.key = "toggle";
  s.type = SettingType::TOGGLE;
  s.valuePtr = &SettingsStub::toggle;
  out.push_back(s);
  s.key = "enum";
  s.type = SettingType::ENUM;
  s.valuePtr = &SettingsStub::enumeration;
  s.labels = {StrId::STR_STATE_OFF, StrId::STR_STATE_ON};
  out.push_back(s);
  s.key = "choices";
  s.enumStringValues = {"A", "B", "C"};
  out.push_back(s);
  s.key = "value";
  s.type = SettingType::VALUE;
  s.valuePtr = &SettingsStub::value;
  out.push_back(s);
  s.key = "text";
  s.type = SettingType::STRING;
  s.stringOffset = offsetof(SettingsStub, text);
  s.stringMaxLen = 16;
  out.push_back(s);
  s.key = "password";
  s.obfuscated = true;
  s.stringOffset = offsetof(SettingsStub, password);
  out.push_back(s);
  s.key = "hidden";
  s.webHidden = true;
  out.push_back(s);
  if (extendedSettings) {
    s = {};
    s.key = "getter-enum";
    s.type = SettingType::ENUM;
    s.labels = {StrId::NAME, StrId::CATEGORY};
    s.valueGetter = [] { return SETTINGS.enumeration; };
    s.valueSetter = [](uint8_t value) { SETTINGS.enumeration = value; };
    out.push_back(s);
    s = {};
    s.key = "getter-string";
    s.type = SettingType::STRING;
    s.stringGetter = [] { return std::string(SETTINGS.text); };
    s.stringSetter = [](const std::string& value) {
      snprintf(SETTINGS.text, sizeof(SETTINGS.text), "%s", value.c_str());
    };
    out.push_back(s);
    s.key = "getter-secret";
    s.obfuscated = true;
    out.push_back(s);
    s = {};
    s.type = SettingType::TOGGLE;
    s.key = "no-pointer";
    out.push_back(s);
    s.type = SettingType::VALUE;
    s.key = "negative-range";
    s.valueRange = {-4, 4, 2};
    out.push_back(s);
    s.type = SettingType::STRING;
    s.key = "no-string";
    out.push_back(s);
    s.type = SettingType::ACTION;
    s.key = "action";
    out.push_back(s);
    s.key = nullptr;
    out.push_back(s);
    s = {};
    s.type = SettingType::ENUM;
    s.key = "oversized";
    s.enumStringValues = {std::string(600, 'x')};
    out.push_back(s);
  }
  return out;
}
struct OpdsServer {
  std::string name, url, username, password;
  struct Header {
    std::string name, value;
  };
  static constexpr size_t MAX_CUSTOM_HEADERS = 2;
  std::array<Header, MAX_CUSTOM_HEADERS> customHeaders;
};
struct OpdsStub {
  std::vector<OpdsServer> servers;
  const auto& getServers() const { return servers; }
  size_t getCount() const { return servers.size(); }
  const OpdsServer* getServer(size_t i) const { return i < servers.size() ? &servers[i] : nullptr; }
  bool addServer(const OpdsServer& s) {
    host::trace.emplace_back("opds-add");
    if (servers.size() >= 8 || host::fail("opds-add")) return false;
    servers.push_back(s);
    return true;
  }
  void updateServer(size_t i, const OpdsServer& s) {
    host::trace.emplace_back("opds-update");
    servers[i] = s;
  }
  void removeServer(size_t i) {
    host::trace.emplace_back("opds-remove");
    servers.erase(servers.begin() + i);
  }
};
inline OpdsStub OPDS_STORE;
struct WifiCredential {
  std::string ssid, password;
};
struct WifiCredentialSummary {
  std::string ssid;
  bool hasPassword, isLastConnected;
};
struct WifiStoreStub {
  std::vector<WifiCredential> credentials;
  std::vector<WifiCredentialSummary> getCredentialSummaries() const {
    std::vector<WifiCredentialSummary> out;
    for (auto& c : credentials) out.push_back({c.ssid, !c.password.empty(), false});
    return out;
  }
  std::optional<WifiCredential> getCredentialAt(size_t i) const {
    return i < credentials.size() ? std::optional(credentials[i]) : std::nullopt;
  }
  std::optional<std::string> getSsidAt(size_t i) const {
    return i < credentials.size() ? std::optional(credentials[i].ssid) : std::nullopt;
  }
  bool addCredential(const std::string& ssid, const std::string& password) {
    host::trace.emplace_back("wifi-add:" + ssid);
    if (host::fail("wifi-add")) return false;
    for (auto& c : credentials)
      if (c.ssid == ssid) {
        c.password = password;
        return true;
      }
    if (credentials.size() >= 8) return false;
    credentials.push_back({ssid, password});
    return true;
  }
  bool removeCredential(const std::string& ssid) {
    host::trace.emplace_back("wifi-remove:" + ssid);
    if (host::fail("wifi-remove")) return false;
    for (auto it = credentials.begin(); it != credentials.end(); ++it)
      if (it->ssid == ssid) {
        credentials.erase(it);
        return true;
      }
    return false;
  }
};
inline WifiStoreStub WIFI_STORE;
namespace PluginLocations {
struct Entry {
  std::string name, dir;
  bool hasPluginJs = true, hasManifest = true;
};
inline std::vector<Entry> entries;
inline auto scanPlugins() { return entries; }
inline std::string findPluginDir(const char* name) {
  for (const auto& e : entries)
    if (e.name == name) return e.dir;
  return {};
}
}  // namespace PluginLocations
namespace pluginhttp {
using Headers = std::vector<std::pair<std::string, std::string>>;
inline void readHeaders(JsonVariantConst value, Headers& out) {
  for (auto p : value.as<JsonObjectConst>()) out.emplace_back(p.key().c_str(), p.value().as<std::string>());
}
inline int request(void*, const std::string& url, const std::string& method, const std::string& body,
                   const Headers& headers, String& out, size_t limit, Headers* response,
                   const std::function<bool()>& poll) {
  host::trace.push_back("relay:" + method + ":" + url + ":" + body + ":" + std::to_string(limit));
  for (auto& h : headers) host::trace.push_back("relay-header:" + h.first + ":" + h.second);
  poll();
  out = "response";
  *response = {{"X-Test", "a"}, {"X-Test", "b"}};
  return host::fail("relay") ? -1 : 201;
}
}  // namespace pluginhttp
namespace freeink {
struct SecureHttpClient {
  void setUserAgent(const char* value) { host::trace.push_back(std::string("fetch-agent:") + value); }
  void setInsecure() { host::trace.emplace_back("fetch-insecure"); }
  void setTimeout(int n) { host::trace.push_back("fetch-timeout:" + std::to_string(n)); }
  void addHeader(const std::string& name, const std::string& value) {
    host::trace.push_back("fetch-header:" + name + ":" + value);
  }
};
struct FetchOptions {
  size_t startOffset = 0;
};
struct FetchSink {
  std::function<bool(const uint8_t*, size_t)> write;
  std::function<bool()> rewind;
};
struct FetchResult {
  int status = 200;
  size_t total = 4;
  bool complete = true;
};
inline FetchResult fetchResult;
inline bool fetchRewind = false, fetchCrossOrigin = false;
inline unsigned long fetchWait = 0;
inline FetchResult fetchResumable(const std::string& url, const FetchOptions& options,
                                  const std::function<void(SecureHttpClient&, bool)>& setup, FetchSink& sink,
                                  const std::function<bool()>& poll) {
  host::trace.push_back("fetch:" + url + ":" + std::to_string(options.startOffset));
  SecureHttpClient http;
  setup(http, true);
  if (fetchCrossOrigin) setup(http, false);
  host::now += fetchWait;
  poll();
  if (fetchRewind && !sink.rewind()) {
    auto result = fetchResult;
    result.complete = false;
    return result;
  }
  auto result = fetchResult;
  const uint8_t bytes[] = {1, 2, 3, 4};
  if (result.status >= 200 && result.status < 300 && !sink.write(bytes, sizeof(bytes))) result.complete = false;
  return result;
}
namespace content {
inline int32_t base64Decode(const char* input, size_t n, uint8_t* out, size_t cap) {
  const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint32_t bits = 0;
  unsigned have = 0;
  size_t size = 0;
  for (size_t i = 0; i < n; ++i) {
    if (input[i] == '=') break;
    auto digit = alphabet.find(input[i]);
    if (digit == std::string::npos) return -1;
    bits = (bits << 6) | digit;
    have += 6;
    if (have >= 8) {
      have -= 8;
      if (size == cap) return -1;
      out[size++] = static_cast<uint8_t>(bits >> have);
    }
  }
  return size;
}
struct RsaKeyPairDer {
  std::vector<uint8_t> spki, pkcs8;
};
struct WolfsslCrypto {
  std::string lastError = "mock failure";
  void randomBytes(uint8_t* p, size_t n) { std::memset(p, 1, n); }
  void sha1(const uint8_t*, size_t, uint8_t* p) { std::memset(p, 2, 20); }
  void sha256(const uint8_t*, size_t, uint8_t* p) { std::memset(p, 3, 32); }
  bool aes128CbcDecrypt(const uint8_t*, const uint8_t*, const uint8_t* p, size_t n, uint8_t* out) {
    std::memcpy(out, p, n);
    return !host::fail("crypto");
  }
  int32_t rsaPrivateRaw(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t* out, size_t n) {
    std::memset(out, 4, n);
    return host::fail("crypto") ? -1 : 128;
  }
  bool rsaGenerate(RsaKeyPairDer* out) {
    out->spki = {1, 2};
    out->pkcs8 = {3, 4};
    return !host::fail("crypto");
  }
  bool rsaPublicEncrypt(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t* out, size_t, size_t* n) {
    *n = 128;
    std::memset(out, 5, 128);
    return !host::fail("crypto");
  }
  bool rsaPrivateSignRaw(const uint8_t*, size_t, const uint8_t*, uint8_t* out) {
    std::memset(out, 6, 128);
    return !host::fail("crypto");
  }
  bool pkcs12Extract(const uint8_t*, size_t, const std::string&, std::vector<uint8_t>* key,
                     std::vector<uint8_t>* cert) {
    *key = {1, 2};
    *cert = {3, 4};
    return !host::fail("crypto");
  }
};
}  // namespace content
}  // namespace freeink
struct Aes {};
constexpr int AES_ENCRYPTION = 1;
inline int wc_AesSetKey(Aes*, const uint8_t*, size_t, const uint8_t*, int) { return host::fail("crypto") ? -1 : 0; }
inline int wc_AesCbcEncrypt(Aes*, uint8_t* out, const uint8_t* input, size_t n) {
  std::memmove(out, input, n);
  return 0;
}
namespace base64 {
inline String encode(const uint8_t* p, size_t n) {
  constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out;
  for (size_t i = 0; i < n; i += 3) {
    unsigned value = unsigned(p[i]) << 16;
    if (i + 1 < n) value |= unsigned(p[i + 1]) << 8;
    if (i + 2 < n) value |= p[i + 2];
    out += alphabet[value >> 18];
    out += alphabet[(value >> 12) & 63];
    out += i + 1 < n ? alphabet[(value >> 6) & 63] : '=';
    out += i + 2 < n ? alphabet[value & 63] : '=';
  }
  return out;
}
}  // namespace base64
namespace bookkey {
constexpr size_t MAX_KEY_LEN = 32;
inline bool write(const std::string& path, const uint8_t*, size_t n, int64_t expires) {
  host::trace.push_back("bookkey:" + path + ":" + std::to_string(n) + ":" + std::to_string(expires));
  return !host::fail("bookkey");
}
}  // namespace bookkey
