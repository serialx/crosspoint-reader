#include "PluginHttp.h"

#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <xp_plugin.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "PluginJson.h"
#include "network/ProtectedPaths.h"
#include "network/WifiPowerSaveGuard.h"

namespace {
XpPluginSpan span(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
XpPluginSpan span(const char* s) { return {reinterpret_cast<const uint8_t*>(s), s ? strlen(s) : 0}; }
constexpr size_t MAX_TOKEN_FILE_SIZE = 2 * 1024;

// Resolves the caller's reused TLS session (or `tmp`) into a configured,
// connected client, or nullptr if begin() failed.
freeink::SecureHttpClient* openClient(freeink::SecureHttpClient* session, freeink::SecureHttpClient& tmp,
                                      const std::string& url, const pluginhttp::Headers& headers) {
  freeink::SecureHttpClient& http = session ? *session : tmp;
  http.setUserAgent("CrossPoint");
  // Same trust posture as every other SecureNet consumer: no CA bundle ships
  // with the transport, so verification is skipped; traffic stays encrypted.
  http.setInsecure();
  http.setTimeout(30000);
  if (!http.begin(url)) return nullptr;
  for (const auto& h : headers) http.addHeader(h.first, h.second);
  return &http;
}
}  // namespace

namespace pluginhttp {

void splitPath(const std::string& dotted, std::vector<std::string>& out) {
  out.clear();
  out.reserve(xp_plugin_split_count(span(dotted)));
  xp_plugin_split(span(dotted), &out, [](void* ctx, XpPluginSpan part) {
    static_cast<std::vector<std::string>*>(ctx)->emplace_back(reinterpret_cast<const char*>(part.data), part.len);
  });
}

JsonVariantConst resolvePath(JsonVariantConst node, const std::string& dotted) {
  PluginJson json(node);
  const auto api = json.api();
  xp_plugin_resolve(&api, span(dotted));
  return json.slots[0];
}

std::string variantToString(JsonVariantConst v) {
  PluginJson json(v);
  const auto api = json.api();
  switch (static_cast<XpPluginValueKind>(xp_plugin_value_kind(&api))) {
    case XP_PLUGIN_VALUE_STRING:
      return v.as<const char*>();
    case XP_PLUGIN_VALUE_INTEGER: {
      char buf[24];
      snprintf(buf, sizeof(buf), "%lld", v.as<long long>());
      return buf;
    }
    case XP_PLUGIN_VALUE_EMPTY:
      return {};
  }
  return {};
}

void substituteAll(std::string& s, const char* key, const std::string& value) {
  const XpPluginString text{&s, [](void* ctx) { return span(*static_cast<std::string*>(ctx)); },
                            [](void* ctx, size_t pos, size_t len, XpPluginSpan value) {
                              static_cast<std::string*>(ctx)->replace(
                                  pos, len, reinterpret_cast<const char*>(value.data), value.len);
                            }};
  xp_plugin_substitute(&text, span(key), span(value), &s == &value);
}

void readRequest(JsonVariantConst node, const char* defaultMethod, RequestSpec& out) {
  PluginJson json(node);
  const auto api = json.api();
  static const XpPluginRequest targetCallbacks{nullptr,
                                               [](void* p, uint32_t field, XpPluginSpan s) {
                                                 auto& r = *static_cast<RequestSpec*>(p);
                                                 std::string* fields[] = {&r.url, &r.method, &r.body};
                                                 fields[field]->assign(reinterpret_cast<const char*>(s.data), s.len);
                                               },
                                               [](void* p, XpPluginSpan key, XpPluginSpan value) {
                                                 static_cast<RequestSpec*>(p)->headers.emplace_back(
                                                     std::string(reinterpret_cast<const char*>(key.data), key.len),
                                                     std::string(reinterpret_cast<const char*>(value.data), value.len));
                                               },
                                               [](void* p, size_t count) {
                                                 auto& h = static_cast<RequestSpec*>(p)->headers;
                                                 h.clear();
                                                 h.reserve(count);
                                               }};
  auto target = targetCallbacks;
  target.ctx = &out;
  xp_plugin_read_request(&api, span(defaultMethod), &target);
}

void readHeaders(JsonVariantConst node, Headers& out) {
  PluginJson json(node);
  const auto api = json.api();
  static const XpPluginRequest targetCallbacks{nullptr, nullptr,
                                               [](void* p, XpPluginSpan key, XpPluginSpan value) {
                                                 static_cast<Headers*>(p)->emplace_back(
                                                     std::string(reinterpret_cast<const char*>(key.data), key.len),
                                                     std::string(reinterpret_cast<const char*>(value.data), value.len));
                                               },
                                               [](void* p, size_t count) {
                                                 auto& h = *static_cast<Headers*>(p);
                                                 h.clear();
                                                 h.reserve(count);
                                               }};
  auto target = targetCallbacks;
  target.ctx = &out;
  xp_plugin_read_headers(&api, &target);
}

std::string urlEncodeQuery(const std::string& s) {
  std::string out;
  out.reserve(s.size() * 3);
  xp_plugin_encode(span(s), &out, [](void* ctx, XpPluginSpan part) {
    static_cast<std::string*>(ctx)->append(reinterpret_cast<const char*>(part.data), part.len);
  });
  return out;
}

std::string inPluginDir(const std::string& pluginDir, const char* path) {
  if (!xp_plugin_relative(span(path))) return path ? path : "";
  return pluginDir + "/" + path;
}

// A manifest may not point the firmware at a credential store.
static bool allowedFile(const std::string& file) {
  if (protectedpaths::isPluginPath(file)) return true;
  LOG_ERR("PHTP", "Manifest file not allowed: %s", file.c_str());
  return false;
}

bool loadTokenFromFile(const std::string& file, const std::string& path, std::string& out) {
  out.clear();
  struct Context {
    const std::string& file;
    const std::string& path;
    std::string& out;
    std::string raw;
    JsonDocument doc;
  } context{file, path, out, {}, JsonDocument{}};
  XpPluginTokenOps ops{};
  ops.ctx = &context;
  ops.allowed = [](void* p) { return allowedFile(static_cast<Context*>(p)->file); };
  ops.read_parse = [](void* p) {
    auto& c = *static_cast<Context*>(p);
    return Storage.readFileToString("PHTP", c.file, MAX_TOKEN_FILE_SIZE, c.raw) &&
           deserializeJson(c.doc, c.raw) == DeserializationError::Ok;
  };
  ops.extract = [](void* p) {
    auto& c = *static_cast<Context*>(p);
    c.out = variantToString(resolvePath(c.doc.as<JsonVariantConst>(), c.path));
    return !c.out.empty();
  };
  return xp_plugin_load_token(file.empty(), &ops);
}

bool saveTokenToFile(const std::string& file, const std::string& path, const std::string& value) {
  struct Context {
    const std::string& file;
    const std::string& value;
    JsonDocument doc;
    JsonVariant node;
    std::string out;
    HalFile handle;
  } context{file, value, JsonDocument{}, {}, {}, {}};
  context.node = context.doc.to<JsonObject>();
  XpPluginTokenOps ops{};
  ops.ctx = &context;
  ops.allowed = [](void* p) { return allowedFile(static_cast<Context*>(p)->file); };
  ops.segment = [](void* p, XpPluginSpan key, bool last) {
    auto& c = *static_cast<Context*>(p);
    const std::string name(reinterpret_cast<const char*>(key.data), key.len);
    if (last)
      c.node[name] = c.value;
    else
      c.node = c.node[name].to<JsonObject>();
  };
  ops.serialize = [](void* p) {
    auto& c = *static_cast<Context*>(p);
    serializeJson(c.doc, c.out);
    return c.out.size();
  };
  ops.open = [](void* p) {
    auto& c = *static_cast<Context*>(p);
    return Storage.openFileForWrite("PHTP", c.file, c.handle);
  };
  ops.write = [](void* p) {
    auto& c = *static_cast<Context*>(p);
    return c.handle.write(c.out.data(), c.out.size());
  };
  ops.flush = [](void* p) { static_cast<Context*>(p)->handle.flush(); };
  ops.close = [](void* p) {
    auto& f = static_cast<Context*>(p)->handle;
    if (f.isOpen()) f.close();
  };
  ops.remove = [](void* p) { Storage.remove(static_cast<Context*>(p)->file.c_str()); };
  ops.short_write = [](void*, size_t written, size_t expected) {
    LOG_ERR("PHTP", "short token write (%u of %u)", static_cast<unsigned>(written), static_cast<unsigned>(expected));
  };
  return xp_plugin_save_token(file.empty(), span(path), &ops);
}

void loadConfigFile(const std::string& file, Headers& out) {
  out.clear();
  if (file.empty()) return;
  if (!allowedFile(file)) return;
  std::string raw;
  if (!Storage.readFileToString("PHTP", file, MAX_TOKEN_FILE_SIZE, raw)) return;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return;
  PluginJson json(doc.as<JsonVariantConst>());
  const auto api = json.api();
  struct Context {
    Headers& out;
    PluginJson& json;
  } context{out, json};
  static const XpPluginConfigOps opsCallbacks{
      nullptr, [](void* p, size_t count) { static_cast<Context*>(p)->out.reserve(count); },
      [](void* p, XpPluginSpan key, uint32_t slot) {
        auto& c = *static_cast<Context*>(p);
        c.out.emplace_back(std::string(reinterpret_cast<const char*>(key.data), key.len),
                           variantToString(c.json.slots[slot]));
      }};
  auto ops = opsCallbacks;
  ops.ctx = &context;
  xp_plugin_config_fields(&api, &ops);
}

int request(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
            const std::string& body, const Headers& headers, String& out, const size_t maxResponse,
            Headers* responseHeaders, const std::function<bool()>& shouldAbort) {
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient tmp;
  freeink::SecureHttpClient* httpPtr = openClient(session, tmp, url, headers);
  if (!httpPtr) return -1;
  freeink::SecureHttpClient& http = *httpPtr;

  out.remove(0);
  size_t reserved = 0;
  bool overflow = false;
  const int status = http.sendRequest(
      method.c_str(), reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&](const uint8_t* data, size_t len) {
        if (!xp_plugin_receive(out.length(), len, maxResponse, &reserved)) {
          overflow = true;
          return false;
        }
        if (!out.reserve(reserved) || !out.concat(reinterpret_cast<const char*>(data), len)) {
          overflow = true;
          return false;
        }
        return true;
      },
      shouldAbort);
  if (responseHeaders) *responseHeaders = http.getHeaders();
  // Error statuses still return their body: OAuth device-code polling carries
  // its state ("authorization_pending") in 4xx response bodies.
  if (!xp_plugin_http_ok(status, overflow, http.responseComplete())) {
    LOG_ERR("PHTP", "API request failed: status=%d overflow=%d complete=%d %s", status, overflow,
            http.responseComplete(), url.c_str());
    return -1;
  }
  return status;
}

int requestToFile(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                  const std::string& body, const Headers& headers, const char* destPath, const size_t maxResponse) {
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient tmp;
  freeink::SecureHttpClient* httpPtr = openClient(session, tmp, url, headers);
  if (!httpPtr) return -1;
  freeink::SecureHttpClient& http = *httpPtr;

  int status = -1;
  bool writeOk = true;
  size_t written = 0;
  {
    HalFile file;
    if (!Storage.openFileForWrite("PHTP", destPath, file)) return -1;
    status = http.sendRequest(method.c_str(), reinterpret_cast<const uint8_t*>(body.data()), body.size(),
                              [&](const uint8_t* data, size_t len) {
                                size_t reserved = 0;
                                if (!xp_plugin_receive(written, len, maxResponse, &reserved)) {
                                  writeOk = false;
                                  return false;
                                }
                                if (file.write(data, len) != len) {
                                  writeOk = false;
                                  return false;
                                }
                                written += len;
                                return true;
                              });
    file.flush();
    // file closed at scope exit, before any Storage.remove of destPath
  }
  if (!xp_plugin_http_ok(status, !writeOk, http.responseComplete())) {
    LOG_ERR("PHTP", "API request (to file) failed: status=%d writeOk=%d complete=%d %s", status, writeOk,
            http.responseComplete(), url.c_str());
    Storage.remove(destPath);
    return -1;
  }
  return status;
}

bool mintPasswordToken(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                       const std::string& body, const Headers& headers, const std::string& tokenPath,
                       std::string& outToken) {
  String response;
  constexpr size_t MAX_AUTH_RESPONSE = 8 * 1024;
  const int status = request(session, url, method, body, headers, response, MAX_AUTH_RESPONSE);
  if (!xp_plugin_mint_status(status)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) return false;
  outToken = variantToString(resolvePath(doc.as<JsonVariantConst>(), tokenPath));
  return !outToken.empty();
}

}  // namespace pluginhttp
