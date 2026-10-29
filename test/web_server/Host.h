#pragma once

#include <ArduinoJson.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <vector>

#include "src/network/ProtectedPaths.h"
#include "stubs/WString.h"

namespace ArduinoJson {
template <>
struct Converter<String> {
  static void toJson(const String& value, JsonVariant dst) { dst.set(static_cast<const std::string&>(value)); }
  static String fromJson(JsonVariantConst src) { return src.as<std::string>(); }
  static bool checkJson(JsonVariantConst src) { return src.is<const char*>(); }
};
}  // namespace ArduinoJson

namespace host {
inline std::string hex(const uint8_t* data, size_t size) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) {
    out += digits[data[i] >> 4];
    out += digits[data[i] & 15];
  }
  return out;
}
inline std::vector<std::string> trace;
inline unsigned long now = 1000;
inline std::string failOperation;
inline int failAfter = -1;
inline bool cancelled = false, connected = true;
inline size_t writeLimit = SIZE_MAX;
inline bool fail(const char* operation) {
  if (failOperation != operation || failAfter < 0) return false;
  if (failAfter-- == 0) return true;
  return false;
}
inline void log(const char* level, const char* origin, const char* format, ...) {
  char bytes[4096];
  va_list args;
  va_start(args, format);
  vsnprintf(bytes, sizeof(bytes), format, args);
  va_end(args);
  trace.push_back(std::string(level) + ":" + origin + ":" +
                  std::regex_replace(std::string(bytes), std::regex("server=0x[0-9a-fA-F]+"), "server=PTR"));
}
struct Node {
  bool directory = false;
  std::string bytes;
};
inline std::map<std::string, std::shared_ptr<Node>> files;
inline void add(const std::string& path, const std::string& bytes = "", bool directory = false) {
  files[path] = std::make_shared<Node>(Node{directory, bytes});
}
}  // namespace host

#define LOG_DBG(tag, ...) host::log("DBG", tag, __VA_ARGS__)
#define LOG_INF(tag, ...) host::log("INF", tag, __VA_ARGS__)
#define LOG_ERR(tag, ...) host::log("ERR", tag, __VA_ARGS__)
inline unsigned long millis() { return host::now; }
inline void delay(unsigned long n) {
  host::now += n;
  host::trace.push_back("delay:" + std::to_string(n));
}
inline void yield() { host::trace.emplace_back("yield"); }
inline void resetTaskWatchdogIfSubscribed() { host::trace.emplace_back("watchdog"); }
inline const char* memoryTranslation = "Memory error";
inline const char* tr(int) { return memoryTranslation; }
inline constexpr int STR_MEMORY_ERROR = 0;
struct Esp {
  unsigned getFreeHeap() const { return 60000; }
  unsigned getMaxAllocHeap() const { return 32000; }
};
inline Esp ESP;

template <class T>
auto makeUniqueNoThrow(size_t n) {
  using Element = std::remove_extent_t<T>;
  host::trace.push_back("alloc:" + std::to_string(n));
  return std::unique_ptr<T>(host::fail("alloc") ? nullptr : new Element[n]);
}

template <class T>
auto makeUniqueNoThrow() {
  return std::unique_ptr<T>(host::fail("alloc") ? nullptr : new T{});
}

class HalFile {
 public:
  std::shared_ptr<host::Node> node;
  std::string path;
  size_t pos = 0, entry = 0;
  HalFile() = default;
  HalFile(std::string p, std::shared_ptr<host::Node> n) : node(std::move(n)), path(std::move(p)) {}
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&& rhs) noexcept {
    close();
    node = std::move(rhs.node);
    path = std::move(rhs.path);
    pos = rhs.pos;
    entry = rhs.entry;
    return *this;
  }
  ~HalFile() { close(); }
  explicit operator bool() const { return bool(node); }
  bool isOpen() const { return bool(node); }
  void flush() { host::trace.push_back("flush:" + path); }
  bool isDirectory() const { return node && node->directory; }
  void close() {
    if (node) host::trace.push_back("close:" + path);
    node.reset();
  }
  size_t size() const { return node ? node->bytes.size() : 0; }
  int available() const { return node ? static_cast<int>(size() - pos) : 0; }
  size_t getName(char* out, size_t cap) const {
    const auto name = path.substr(path.rfind('/') + 1);
    if (cap) {
      std::strncpy(out, name.c_str(), cap);
      out[cap - 1] = 0;
    }
    return name.size();
  }
  HalFile openNextFile() {
    size_t i = 0;
    for (auto& [name, n] : host::files) {
      auto parent = name.substr(0, name.rfind('/'));
      if (parent.empty()) parent = "/";
      if (name != path && parent == path && i++ == entry) {
        ++entry;
        host::trace.push_back("next:" + name);
        return HalFile(name, n);
      }
    }
    return {};
  }
  int read(uint8_t* data, size_t n) {
    host::trace.push_back("read:" + path + ":" + std::to_string(n));
    if (!node || host::fail("read")) return -1;
    n = std::min(n, size() - pos);
    std::memcpy(data, node->bytes.data() + pos, n);
    pos += n;
    return n;
  }
  size_t write(const uint8_t* data, size_t n) {
    host::trace.push_back("write:" + path + ":" + std::to_string(n));
    if (!node || host::fail("write")) return 0;
    n = std::min(n, host::writeLimit);
    node->bytes.append(reinterpret_cast<const char*>(data), n);
    pos += n;
    return n;
  }
};

struct StorageStub {
  bool exists(const char* p) const {
    host::trace.push_back(std::string("exists:") + p);
    return host::files.count(p);
  }
  HalFile open(const char* p) const {
    host::trace.push_back(std::string("open:") + p);
    if (host::fail("open")) return {};
    auto it = host::files.find(p);
    return it == host::files.end() ? HalFile{} : HalFile(p, it->second);
  }
  bool openFileForWrite(const char*, const std::string& p, HalFile& file) const {
    host::trace.push_back("create:" + p);
    if (host::fail("create")) return false;
    host::add(p);
    file = HalFile(p, host::files[p]);
    return true;
  }
  HalFile open(const char* p, int flags) const {
    auto file = open(p);
    if (flags & 2) file.pos = file.size();
    return file;
  }
  bool openFileForRead(const char*, const char* p, HalFile& file) const {
    file = open(p);
    return bool(file);
  }
  bool readFileToString(const char*, const std::string& p, size_t cap, std::string& out) const {
    auto file = open(p.c_str());
    if (!file || file.size() > cap) return false;
    out = file.node->bytes;
    return true;
  }
  bool mkdir(const char* p) const {
    host::trace.push_back(std::string("mkdir:") + p);
    if (host::fail("mkdir")) return false;
    host::add(p, "", true);
    return true;
  }
  bool remove(const char* p) const {
    host::trace.push_back(std::string("remove:") + p);
    return !host::fail("remove") && host::files.erase(p);
  }
  bool rmdir(const char* p) const {
    host::trace.push_back(std::string("rmdir:") + p);
    return remove(p);
  }
  void ensureDirectoryExists(const char* p) const {
    host::trace.push_back(std::string("ensure:") + p);
    host::add(p, "", true);
  }
  bool replaceFile(const char* from, const char* to) const {
    host::trace.push_back(std::string("replace:") + from + ":" + to);
    if (host::fail("replace") || !host::files.count(from)) return false;
    host::files[to] = host::files[from];
    host::files.erase(from);
    return true;
  }
};
inline StorageStub Storage;
inline void clearBookCache(const char* p) { host::trace.push_back(std::string("cache:") + p); }
inline bool removeBookFile(const char* p) {
  host::trace.push_back(std::string("removeBook:") + p);
  return Storage.remove(p);
}
namespace ClippingStore {
inline bool moveBook(const char* from, const char* to) { return Storage.replaceFile(from, to); }
}  // namespace ClippingStore
namespace library {
inline bool isLibraryBookFile(const std::string& p) {
  return p.ends_with(".epub") || p.ends_with(".txt") || p.ends_with(".md") || p.ends_with(".xtc") ||
         p.ends_with(".xtch");
}
inline void markLibraryIndexDirty() { host::trace.emplace_back("library-dirty"); }
}  // namespace library
struct SettingsStub {
  bool showHiddenFiles = false, pluginsEnabled = true;
  uint8_t toggle = 0, enumeration = 0, value = 2;
  char text[16] = "original", password[16] = "secret";
  void saveToFile() const { host::trace.emplace_back("settings-save"); }
};
inline SettingsStub SETTINGS;

enum HTTPMethod { HTTP_GET, HTTP_POST, HTTP_OPTIONS };
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload {
  HTTPUploadStatus status = UPLOAD_FILE_START;
  String filename;
  const uint8_t* buf = nullptr;
  size_t currentSize = 0;
};
inline constexpr size_t CONTENT_LENGTH_UNKNOWN = SIZE_MAX;
struct NetworkClient {
  void stop() {
    host::connected = false;
    host::trace.emplace_back("client-stop");
  }
  bool connected() const { return host::connected; }
  size_t write(const uint8_t* data, size_t n) const {
    host::trace.push_back("client-write:" + std::to_string(n) + ":" + host::hex(data, n));
    return host::fail("client-write") ? 0 : n;
  }
  void clear() { host::trace.emplace_back("client-clear"); }
};
class WebServer {
 public:
  explicit WebServer(uint16_t = 80) {}
  virtual ~WebServer() = default;
  String* _currentArgs = nullptr;
  String* _postArgs = nullptr;
  int _currentArgCount = 0, _postArgsLen = 0;
  std::map<std::string, String> headers;
  std::map<std::string, std::function<void()>> routes;
  std::map<std::string, String> args;
  String requestUri;
  HTTPMethod requestMethod = HTTP_GET;
  HTTPUpload part;
  String header(const char* name) const {
    auto it = headers.find(name);
    return it == headers.end() ? String{} : it->second;
  }
  void enableCORS(bool) { host::trace.emplace_back("cors"); }
  void on(const char* uri, HTTPMethod method, std::function<void()> handler, std::function<void()> = {}) {
    host::trace.push_back("route:" + std::to_string(method) + ":" + uri);
    routes[std::to_string(method) + ":" + uri] = std::move(handler);
  }
  void onNotFound(std::function<void()>) { host::trace.emplace_back("route:fallback"); }
  void collectHeaders(const char* const* names, size_t n) {
    for (size_t i = 0; i < n; ++i) host::trace.push_back(std::string("collect-header:") + names[i]);
  }
  template <class T>
  void addHandler(T* p) {
    delete p;
    host::trace.emplace_back("dav");
  }
  void begin() { host::trace.emplace_back("server-begin"); }
  void stop() { host::trace.emplace_back("server-stop"); }
  void handleClient() { host::trace.emplace_back("server-client"); }
  bool hasArg(const char* name) const { return args.count(name); }
  String arg(const char* name) const {
    auto it = args.find(name);
    return it == args.end() ? String{} : it->second;
  }
  String uri() const { return requestUri; }
  HTTPMethod method() const { return requestMethod; }
  HTTPUpload& upload() { return part; }
  NetworkClient client() const { return {}; }
  void setContentLength(size_t n) const {
    host::trace.push_back("length:" + (n == SIZE_MAX ? std::string("chunked") : std::to_string(n)));
  }
  void send(int code, const char* type, const String& body) const {
    host::trace.push_back("response:" + std::to_string(code) + ":" + type + ":" + body);
  }
  void sendContent(const String& body) const { host::trace.push_back("chunk:" + body); }
  void send(int code) const { send(code, "text/html", ""); }
  void send_P(int code, const char* type, const char* data, size_t n) const {
    std::string hex;
    hex.reserve(n * 2);
    constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
      auto c = static_cast<uint8_t>(data[i]);
      hex += digits[c >> 4];
      hex += digits[c & 15];
    }
    host::trace.push_back("response-hex:" + std::to_string(code) + ":" + type + ":" + hex);
  }
  void sendContent(const char* body, size_t n) const { sendContent(String(body, n)); }
  void sendHeader(const char* name, const String& value, bool = false) const {
    host::trace.push_back(std::string("header:") + name + ":" + value);
  }
};
enum WStype_t { WStype_DISCONNECTED, WStype_CONNECTED, WStype_TEXT, WStype_BIN };
struct WebSocketsServer {
  void sendTXT(uint8_t num, const uint8_t* data, size_t len) {
    sendTXT(num, String(reinterpret_cast<const char*>(data), len));
  }
  explicit WebSocketsServer(uint16_t = 81) {}
  void begin() { host::trace.emplace_back("ws-begin"); }
  void close() { host::trace.emplace_back("ws-close"); }
  void loop() { host::trace.emplace_back("ws-loop"); }
  void onEvent(void (*)(uint8_t, WStype_t, uint8_t*, size_t)) {}
  std::function<void(uint8_t, const String&)> onSend;
  void sendTXT(uint8_t num, const String& text) {
    host::trace.push_back("ws:" + std::to_string(num) + ":" + text);
    if (onSend) onSend(num, text);
  }
};
struct NetworkUDP {
  bool begin(uint16_t port) {
    host::trace.push_back("udp-begin:" + std::to_string(port));
    return !host::fail("udp");
  }
  void stop() { host::trace.emplace_back("udp-stop"); }
  int parsePacket() const { return 0; }
  int read(char*, size_t) const { return 0; }
  int remoteIP() const { return 0; }
  int remotePort() const { return 0; }
  void beginPacket(int, int) {}
  void write(const uint8_t*, size_t) {}
  void endPacket() {}
};

// These helpers are supplied from the repository source by the extractor.
namespace FsHelpers {
std::string normalisePath(std::string_view path);
bool isSafePathComponent(std::string_view name);
bool checkFileExtension(std::string_view name, const char* extension);
inline bool hasEpubExtension(const String& name) { return checkFileExtension(name, ".epub"); }
}  // namespace FsHelpers
