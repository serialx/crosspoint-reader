#include "PluginEvents.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <esp_random.h>
#include <time.h>
#include <xp_plugin.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "PluginHttp.h"
#include "PluginJson.h"
#include "PluginLocations.h"
#include "components/UITheme.h"
#include "network/ProtectedPaths.h"

namespace {

XpPluginSpan span(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
XpPluginSpan span(const char* s) { return {reinterpret_cast<const uint8_t*>(s), s ? strlen(s) : 0}; }
constexpr const char* OUTBOX_NAME = "/events.jsonl";
// Drop-oldest wholesale: a plugin that is never drained must not grow a file
// forever, and by the time 4KB of events piled up the old ones describe stale
// state anyway.
constexpr size_t MAX_OUTBOX_BYTES = 4 * 1024;
constexpr size_t MAX_MANIFEST_SIZE = 8 * 1024;
constexpr size_t MAX_EVENT_LINE = 512;
// Event handler responses are acknowledgements, not content.
constexpr size_t MAX_EVENT_RESPONSE = 8 * 1024;

// Static subscription table: one slot per installed plugin that declares an
// "events" section. Rebuilt by refreshSubscriptions(); sized for a full
// plugin list screen, not a marketplace.
constexpr size_t MAX_EVENT_PLUGINS = 8;
using Subscriber = XpPluginSubscriber;
Subscriber subscribers[MAX_EVENT_PLUGINS]{};
int eventFromName(const char* name) { return xp_plugin_event(span(name)); }

std::string outboxPath(const Subscriber& sub) { return std::string(sub.dir) + OUTBOX_NAME; }

}  // namespace

namespace pluginevents {

void refreshSubscriptions() {
  struct Context {
    std::vector<PluginLocations::Entry> entries = PluginLocations::scanPlugins();
    std::string raw;
    JsonDocument doc;
    mutable JsonObjectConstIterator cursor;
    mutable size_t cursorIndex = 0;
    JsonObjectConstIterator seek(size_t index) const {
      auto object = doc["events"].as<JsonObjectConst>();
      if (index == 0 || index < cursorIndex) {
        cursor = object.begin();
        cursorIndex = 0;
      }
      while (cursorIndex < index && cursor != object.end()) {
        ++cursor;
        ++cursorIndex;
      }
      return cursor;
    }
    const char* eventKey(size_t index) const {
      auto it = seek(index);
      return it == doc["events"].as<JsonObjectConst>().end() ? nullptr : it->key().c_str();
    }
    JsonVariantConst eventValue(size_t index) const {
      auto it = seek(index);
      return it == doc["events"].as<JsonObjectConst>().end() ? JsonVariantConst{} : it->value();
    }
  } context;
  static const XpPluginSubscriptionOps opsCallbacks{
      nullptr,
      [](void* p) { return static_cast<Context*>(p)->entries.size(); },
      [](void* p, size_t i) { return static_cast<Context*>(p)->entries[i].hasDevice; },
      [](void* p, size_t i) { return span(static_cast<Context*>(p)->entries[i].name); },
      [](void* p, size_t i) { return span(static_cast<Context*>(p)->entries[i].dir); },
      [](void* p, size_t i) {
        auto& c = *static_cast<Context*>(p);
        if (!Storage.readFileToString("PEVT", c.entries[i].dir + "/device.json", MAX_MANIFEST_SIZE, c.raw))
          return false;
        JsonDocument filter;
        filter["events"] = true;
        return deserializeJson(c.doc, c.raw, DeserializationOption::Filter(filter)) == DeserializationError::Ok;
      },
      [](void* p, size_t i) { return span(static_cast<Context*>(p)->eventKey(i)); },
      [](void* p, size_t i) { return static_cast<Context*>(p)->eventValue(i)["connect"] | false; },
      [](void* p) {
        auto& c = *static_cast<Context*>(p);
        c.doc.clear();
        std::string{}.swap(c.raw);
      },
      [](void* p, uint32_t event, size_t i, size_t value) {
        const auto& c = *static_cast<Context*>(p);
        switch (static_cast<XpPluginSubscriptionDiagnostic>(event)) {
          case XP_PLUGIN_SUB_FULL:
            LOG_ERR("PEVT", "subscription table full; ignoring %s", c.entries[i].name.c_str());
            break;
          case XP_PLUGIN_SUB_LONG:
            LOG_ERR("PEVT", "plugin name/path too long for events; ignoring %s", c.entries[i].name.c_str());
            break;
          case XP_PLUGIN_SUB_UNKNOWN:
            LOG_DBG("PEVT", "%s: unknown event '%s' ignored", c.entries[i].name.c_str(), c.eventKey(value));
            break;
          case XP_PLUGIN_SUB_ADDED:
            LOG_DBG("PEVT", "%s subscribes mask=0x%02x", c.entries[i].name.c_str(), static_cast<unsigned>(value));
            break;
        }
      }};
  auto ops = opsCallbacks;
  ops.ctx = &context;
  xp_plugin_subscriptions(subscribers, &ops);
}

bool anySubscriber(Event e) { return xp_plugin_any(subscribers, static_cast<uint32_t>(e)); }
uint8_t subscriptionMask(const char* plugin) { return xp_plugin_mask(subscribers, span(plugin)); }
bool wantsConnectAny() {
  std::string raw;
  static const XpPluginConnectOps opsCallbacks{
      nullptr, [](void* p, const Subscriber* sub) {
        auto& raw = *static_cast<std::string*>(p);
        std::string{}.swap(raw);
        if (!Storage.readFileToString("PEVT", outboxPath(*sub), MAX_OUTBOX_BYTES + MAX_EVENT_LINE, raw))
          return XpPluginSpan{};
        return span(raw);
      }};
  auto ops = opsCallbacks;
  ops.ctx = &raw;
  return xp_plugin_wants_connect(subscribers, &ops);
}

namespace {
void appendEvent(const Subscriber& sub, const std::string& line, size_t lineSize) {
  const std::string path = outboxPath(sub);
  struct AppendContext {
    const Subscriber& sub;
    const std::string& path;
    const std::string& line;
    HalFile file;
  } context{sub, path, line, {}};
  static const XpPluginAppendOps opsCallbacks{
      nullptr,
      [](void* p) {
        auto& c = *static_cast<AppendContext*>(p);
        c.file = Storage.open(c.path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
        return c.file && c.file.isOpen();
      },
      [](void* p) { static_cast<AppendContext*>(p)->file.close(); },
      [](void* p) { Storage.remove(static_cast<AppendContext*>(p)->path.c_str()); },
      [](void* p) { return static_cast<AppendContext*>(p)->file.fileSize64(); },
      [](void* p) {
        auto& c = *static_cast<AppendContext*>(p);
        return c.file.write(reinterpret_cast<const uint8_t*>(c.line.data()), c.line.size());
      },
      [](void* p, uint64_t n) { static_cast<AppendContext*>(p)->file.truncate(n); },
      [](void* p) { static_cast<AppendContext*>(p)->file.flush(); },
      [](void* p, uint32_t event) {
        const auto& sub = static_cast<AppendContext*>(p)->sub;
        switch (static_cast<XpPluginAppendDiagnostic>(event)) {
          case XP_PLUGIN_APPEND_OPEN:
            LOG_ERR("PEVT", "%s: outbox open failed", sub.name);
            break;
          case XP_PLUGIN_APPEND_CAP:
            LOG_DBG("PEVT", "%s: outbox over cap, dropped", sub.name);
            break;
          case XP_PLUGIN_APPEND_SHORT:
            LOG_ERR("PEVT", "%s: short outbox append; event dropped", sub.name);
            break;
        }
      }};
  auto ops = opsCallbacks;
  ops.ctx = &context;
  xp_plugin_append(&ops, lineSize);
}
}  // namespace

void emit(const Event e, const Var* vars, const size_t varCount) {
  if (!anySubscriber(e)) return;
  static XpPluginSequence sequence{};
  struct Context {
    JsonDocument doc;
    JsonVariant node;
    std::string line;
  } context;
  context.node = context.doc.to<JsonObject>();
  static const XpPluginEmitOps opsCallbacks{
      nullptr,
      [](void*) {
        static const uint32_t nonce = esp_random();
        return nonce;
      },
      [](void*) { return static_cast<int64_t>(time(nullptr)); },
      [](void* p, const char* key, const char* value, bool staticKey) {
        auto& c = *static_cast<Context*>(p);
        c.node[JsonString(key, staticKey)] = JsonString(value, false);
      },
      [](void* p, const char* key, int64_t value) { static_cast<Context*>(p)->node[JsonString(key, true)] = value; },
      [](void* p, const char* key) {
        auto& c = *static_cast<Context*>(p);
        c.node = c.node[JsonString(key, true)].to<JsonObject>();
      },
      [](void* p) {
        auto& c = *static_cast<Context*>(p);
        c.line.reserve(160);
        serializeJson(c.doc, c.line);
        c.line += '\n';
        return c.line.size();
      },
      [](void* p, const Subscriber* sub, size_t size) { appendEvent(*sub, static_cast<Context*>(p)->line, size); },
      [](void*, size_t size) { LOG_ERR("PEVT", "event line too large (%u); dropped", static_cast<unsigned>(size)); }};
  auto ops = opsCallbacks;
  ops.ctx = &context;
  static_assert(sizeof(Var) == sizeof(XpPluginVar));
  static_assert(offsetof(Var, key) == offsetof(XpPluginVar, key));
  static_assert(offsetof(Var, value) == offsetof(XpPluginVar, value));
  xp_plugin_emit(subscribers, &sequence, static_cast<uint32_t>(e), reinterpret_cast<const XpPluginVar*>(vars), varCount,
                 &ops);
}

namespace {

// The manifest subset a drain needs: the handlers plus the token/config/auth
// vocabulary shared with the catalog browser.
struct DrainManifest {
  std::string tokenFile, tokenPath, configFile;
  std::string authType, authTokenPath;
  pluginhttp::RequestSpec authReq;
  struct Handler {
    int event = -1;
    pluginhttp::RequestSpec req;
    std::string toast;
    // "download" variant: the response streams to `dest` on SD (e.g. a fresh
    // sleep image) instead of being read as an acknowledgement.
    std::string dest;
    bool isDownload() const { return !dest.empty(); }
  };
  std::vector<Handler> handlers;
  bool hasPasswordGrant() const { return xp_plugin_password(span(authType), span(authReq.url)); }
};

// False only when device.json cannot be read or parsed; a manifest that parses
// but declares no runnable handlers returns true with out.handlers empty.
bool loadDrainManifest(const Subscriber& sub, DrainManifest& out) {
  std::string raw;
  if (!Storage.readFileToString("PEVT", std::string(sub.dir) + "/device.json", MAX_MANIFEST_SIZE, raw)) return false;
  // Filtered parse: the drain runs at the heap-worst moments (sleep entry,
  // web session), so only the sections it reads are materialized.
  JsonDocument filter;
  filter["token"] = true;
  filter["config"] = true;
  filter["auth"] = true;
  filter["events"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, raw, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return false;

  PluginJson json(doc.as<JsonVariantConst>());
  const auto api = json.api();
  struct Context {
    const Subscriber& sub;
    DrainManifest& out;
    PluginJson& json;
    DrainManifest::Handler handler;
  } context{sub, out, json, {}};
  static const XpPluginManifestOps opsCallbacks{
      nullptr,
      [](void* p, uint32_t field, XpPluginSpan value) {
        auto& c = *static_cast<Context*>(p);
        const auto* text = reinterpret_cast<const char*>(value.data);
        switch (static_cast<XpPluginManifestField>(field)) {
          case XP_PLUGIN_TOKEN_FILE:
            c.out.tokenFile = pluginhttp::inPluginDir(c.sub.dir, value.len ? text : "");
            break;
          case XP_PLUGIN_TOKEN_PATH:
            c.out.tokenPath.assign(text, value.len);
            break;
          case XP_PLUGIN_CONFIG_FILE:
            c.out.configFile = pluginhttp::inPluginDir(c.sub.dir, value.len ? text : "");
            break;
          case XP_PLUGIN_AUTH_TYPE:
            c.out.authType.assign(text, value.len);
            break;
          case XP_PLUGIN_AUTH_TOKEN_PATH:
            c.out.authTokenPath.assign(text, value.len);
            break;
          case XP_PLUGIN_TOAST:
            c.handler.toast.assign(text, value.len);
            break;
          case XP_PLUGIN_DEST:
            c.handler.dest.assign(text, value.len);
            break;
        }
      },
      [](void* p, uint32_t field, uint32_t slot, const char* method) {
        auto& c = *static_cast<Context*>(p);
        pluginhttp::readRequest(c.json.slots[slot], method, field == 0 ? c.out.authReq : c.handler.req);
      },
      [](void* p, size_t n) { static_cast<Context*>(p)->out.handlers.reserve(n); },
      [](void* p, int32_t event) {
        auto& h = static_cast<Context*>(p)->handler;
        h = DrainManifest::Handler{};
        h.event = event;
      },
      [](void* p) {
        auto& c = *static_cast<Context*>(p);
        c.out.handlers.push_back(std::move(c.handler));
      }};
  auto ops = opsCallbacks;
  ops.ctx = &context;
  xp_plugin_manifest(&api, &ops);
  return true;
}

// {token}, {cfg.*}, {meta.*}, and {event.*} from the queued line's vars
// object. `config` and `meta` hold pre-built patterns ("{cfg.KEY}" /
// "{meta.KEY}") so the keys are not re-concatenated for every template.
std::string drainSubstituted(std::string tpl, const std::string& token, const pluginhttp::Headers& config,
                             const pluginhttp::Headers& meta, JsonVariantConst vars, const long long ts,
                             const char* id) {
  char timestamp[16];
  snprintf(timestamp, sizeof(timestamp), "%lld", ts);
  struct Context {
    std::string& text;
    const pluginhttp::Headers& config;
    const pluginhttp::Headers& meta;
    JsonObjectConst vars;
    std::string value;
    JsonObjectConstIterator cursor;
    size_t cursorIndex = 0;
  } context{tpl, config, meta, vars.as<JsonObjectConst>(), {}, {}, 0};
  static const XpPluginSubstitutionOps opsCallbacks{
      nullptr,
      [](void* p, uint32_t group, size_t i, XpPluginSpan* key, XpPluginSpan* value) {
        auto& c = *static_cast<Context*>(p);
        if (group < 2) {
          const auto& pairs = group == 0 ? c.config : c.meta;
          if (i >= pairs.size()) return false;
          *key = span(pairs[i].first);
          *value = span(pairs[i].second);
        } else {
          if (i == 0) {
            c.cursor = c.vars.begin();
            c.cursorIndex = 0;
          }
          while (c.cursorIndex < i && c.cursor != c.vars.end()) {
            ++c.cursor;
            ++c.cursorIndex;
          }
          if (c.cursor == c.vars.end()) return false;
          *key = span(c.cursor->key().c_str());
          std::string{}.swap(c.value);
          c.value = pluginhttp::variantToString(c.cursor->value());
          *value = span(c.value);
        }
        return true;
      },
      [](void* p, XpPluginSpan key, XpPluginSpan value, bool event) {
        auto& c = *static_cast<Context*>(p);
        std::string pattern;
        if (event) {
          pattern = "{event." + std::string(reinterpret_cast<const char*>(key.data), key.len) + "}";
          key = span(pattern);
        }
        const XpPluginString text{&c.text, [](void* p) { return span(*static_cast<std::string*>(p)); },
                                  [](void* p, size_t pos, size_t len, XpPluginSpan value) {
                                    static_cast<std::string*>(p)->replace(
                                        pos, len, reinterpret_cast<const char*>(value.data), value.len);
                                  }};
        xp_plugin_substitute(&text, key, value, false);
      }};
  auto ops = opsCallbacks;
  ops.ctx = &context;
  xp_plugin_event_substitute(span(token), span(timestamp), span(id), &ops);
  return tpl;
}

// Replays one queued line. True = delivered (drop the line); false = transport
// or auth failure (keep it for the next drain). A line with no matching
// handler counts as delivered so a manifest edit can't wedge the queue.
// `token` is shared across the drain: a 401-minted refresh persists to the
// remaining lines instead of re-minting per line.
bool deliverLine(const DrainManifest& mf, const std::string& lineText, std::string& token,
                 const pluginhttp::Headers& config, GfxRenderer* renderer) {
  JsonDocument doc;
  const bool parsed = deserializeJson(doc, lineText) == DeserializationError::Ok;
  const size_t selected = xp_plugin_pick_handler(
      parsed, eventFromName(doc["e"] | ""), mf.handlers.size(), const_cast<DrainManifest*>(&mf),
      [](void* p, size_t i) -> int32_t { return static_cast<DrainManifest*>(p)->handlers[i].event; });
  if (selected == SIZE_MAX) return true;
  const auto* handler = &mf.handlers[selected];

  JsonVariantConst vars = doc["vars"];
  const long long ts = doc["ts"] | 0LL;
  // Lines queued by pre-id firmware substitute {event.id} as empty.
  const char* id = doc["id"] | "";

  // Book-scoped events expose the book's plugin sidecar ("<book>.meta.json",
  // flat fields written at download time) as {meta.*} variables, e.g. a
  // service book id for a sync handler.
  pluginhttp::Headers meta;
  const char* book = vars["book"] | "";
  if (book[0] != '\0') {
    pluginhttp::loadConfigFile(std::string(book) + ".meta.json", meta);
    for (auto& kv : meta) kv.first = "{meta." + kv.first + "}";
  }

  const auto run = [&](const std::string& tok) {
    pluginhttp::Headers headers;
    headers.reserve(handler->req.headers.size());
    for (const auto& h : handler->req.headers) {
      headers.emplace_back(h.first, drainSubstituted(h.second, tok, config, meta, vars, ts, id));
    }
    std::string dest;
    const auto allowed = [&]() {
      dest = drainSubstituted(handler->dest, tok, config, meta, vars, ts, id);
      return protectedpaths::isPluginPath(dest);
    };
    const auto download = [&]() {
      // Generous for images (a 4-bit 800x480 BMP is ~192KB), still bounded.
      constexpr size_t MAX_EVENT_DOWNLOAD = 1024 * 1024;
      // Stream to a sibling temp and swap it in only on a clean 2xx, so a
      // 404/500 error body can never replace an existing dest (e.g. /sleep.bmp).
      const std::string tmp = dest + ".part";
      const int st = pluginhttp::requestToFile(
          nullptr, drainSubstituted(handler->req.url, tok, config, meta, vars, ts, id), handler->req.method,
          drainSubstituted(handler->req.body, tok, config, meta, vars, ts, id), headers, tmp.c_str(),
          MAX_EVENT_DOWNLOAD);
      struct InstallContext {
        const std::string& dest;
        const std::string& tmp;
        std::string bak;
      } context{dest, tmp, {}};
      static const XpPluginInstallOps opsCallbacks{
          nullptr,
          [](void* p) { return Storage.exists(static_cast<InstallContext*>(p)->dest.c_str()); },
          [](void* p) {
            auto& c = *static_cast<InstallContext*>(p);
            if (c.bak.empty()) c.bak = c.dest + ".bak";
            Storage.remove(c.bak.c_str());
          },
          [](void* p) {
            auto& c = *static_cast<InstallContext*>(p);
            return Storage.rename(c.dest.c_str(), c.bak.c_str());
          },
          [](void* p) {
            auto& c = *static_cast<InstallContext*>(p);
            return Storage.rename(c.tmp.c_str(), c.dest.c_str());
          },
          [](void* p) {
            auto& c = *static_cast<InstallContext*>(p);
            return Storage.rename(c.bak.c_str(), c.dest.c_str());
          },
          [](void* p) { Storage.remove(static_cast<InstallContext*>(p)->tmp.c_str()); },
          [](void* p) {
            const auto& dest = static_cast<InstallContext*>(p)->dest;
            if (library::isLibraryBookFile(dest)) library::markLibraryIndexDirty();
          },
          [](void* p) { LOG_ERR("PEVT", "restore of %s failed", static_cast<InstallContext*>(p)->dest.c_str()); }};
      auto ops = opsCallbacks;
      ops.ctx = &context;
      return xp_plugin_install_download(st, &ops);
    };
    const auto response = [&]() {
      String body;
      return pluginhttp::request(
          nullptr, drainSubstituted(handler->req.url, tok, config, meta, vars, ts, id), handler->req.method,
          drainSubstituted(handler->req.body, tok, config, meta, vars, ts, id), headers, body, MAX_EVENT_RESPONSE);
    };
    struct RunContext {
      const decltype(allowed)& allowedFn;
      const decltype(download)& downloadFn;
      const decltype(response)& responseFn;
      const std::string& dest;
    } context{allowed, download, response, dest};
    static const XpPluginRunOps opsCallbacks{
        nullptr, [](void* p) { return static_cast<RunContext*>(p)->allowedFn(); },
        [](void* p) -> int32_t { return static_cast<RunContext*>(p)->downloadFn(); },
        [](void* p) -> int32_t { return static_cast<RunContext*>(p)->responseFn(); },
        [](void* p) {
          LOG_ERR("PEVT", "unsafe download dest rejected: %s", static_cast<RunContext*>(p)->dest.c_str());
        }};
    auto ops = opsCallbacks;
    ops.ctx = &context;
    return xp_plugin_run(handler->isDownload(), &ops);
  };

  std::string minted;
  const auto mint = [&]() {
    pluginhttp::Headers authHeaders;
    authHeaders.reserve(mf.authReq.headers.size());
    for (const auto& h : mf.authReq.headers) {
      authHeaders.emplace_back(h.first, drainSubstituted(h.second, token, config, meta, vars, ts, id));
    }
    return pluginhttp::mintPasswordToken(
        nullptr, drainSubstituted(mf.authReq.url, token, config, meta, vars, ts, id), mf.authReq.method,
        drainSubstituted(mf.authReq.body, token, config, meta, vars, ts, id), authHeaders, mf.authTokenPath, minted);
  };
  const auto toast = [&]() {
    GUI.drawPopup(*renderer, drainSubstituted(handler->toast, token, config, meta, vars, ts, id).c_str());
  };
  struct DeliveryContext {
    const decltype(run)& runFn;
    const decltype(mint)& mintFn;
    const decltype(toast)& toastFn;
    const DrainManifest& manifest;
    std::string& token;
    std::string& minted;
  } context{run, mint, toast, mf, token, minted};
  static const XpPluginDeliveryOps opsCallbacks{nullptr,
                                                [](void* p) -> int32_t {
                                                  auto& c = *static_cast<DeliveryContext*>(p);
                                                  return c.runFn(c.token);
                                                },
                                                [](void* p) { return static_cast<DeliveryContext*>(p)->mintFn(); },
                                                [](void* p) {
                                                  auto& c = *static_cast<DeliveryContext*>(p);
                                                  pluginhttp::saveTokenToFile(c.manifest.tokenFile,
                                                                              c.manifest.tokenPath, c.minted);
                                                },
                                                [](void* p) {
                                                  auto& c = *static_cast<DeliveryContext*>(p);
                                                  c.token = c.minted;
                                                },
                                                [](void* p) { static_cast<DeliveryContext*>(p)->toastFn(); }};
  auto ops = opsCallbacks;
  ops.ctx = &context;
  return xp_plugin_deliver(mf.hasPasswordGrant(), renderer && !handler->toast.empty(), &ops);
}

}  // namespace

namespace {
void drainFile(const Subscriber& sub, GfxRenderer* renderer, size_t* budget) {
  struct Context {
    const Subscriber& sub;
    GfxRenderer* renderer;
    std::string path, raw;
    DrainManifest manifest;
    std::string token;
    pluginhttp::Headers config;
  } context{sub, renderer, outboxPath(sub), {}, {}, {}, {}};
  static const XpPluginDrainFileOps opsCallbacks{
      nullptr,
      [](void* p) { return Storage.exists(static_cast<Context*>(p)->path.c_str()); },
      [](void* p) {
        auto& c = *static_cast<Context*>(p);
        return Storage.readFileToString("PEVT", c.path, MAX_OUTBOX_BYTES + MAX_EVENT_LINE, c.raw);
      },
      [](void* p) {
        auto& c = *static_cast<Context*>(p);
        return loadDrainManifest(c.sub, c.manifest);
      },
      [](void* p) { return static_cast<Context*>(p)->manifest.handlers.size(); },
      [](void* p) { Storage.remove(static_cast<Context*>(p)->path.c_str()); },
      [](void* p) {
        auto& c = *static_cast<Context*>(p);
        pluginhttp::loadTokenFromFile(c.manifest.tokenFile, c.manifest.tokenPath, c.token);
        pluginhttp::loadConfigFile(c.manifest.configFile, c.config);
        for (auto& kv : c.config) kv.first = "{cfg." + kv.first + "}";
      },
      [](void* p, size_t* budget) {
        auto& c = *static_cast<Context*>(p);
        const XpPluginDrainOps lines{p,
                                     [](void* p, XpPluginSpan line) {
                                       auto& c = *static_cast<Context*>(p);
                                       return deliverLine(
                                           c.manifest, std::string(reinterpret_cast<const char*>(line.data), line.len),
                                           c.token, c.config, c.renderer);
                                     },
                                     [](void* p) { Storage.remove(static_cast<Context*>(p)->path.c_str()); },
                                     [](void* p, size_t pos) {
                                       auto& c = *static_cast<Context*>(p);
                                       return Storage.writeFile(c.path.c_str(), String(c.raw.c_str() + pos));
                                     },
                                     [](void* p, uint32_t event, bool stalled) {
                                       const auto& sub = static_cast<Context*>(p)->sub;
                                       switch (static_cast<XpPluginDrainDiagnostic>(event)) {
                                         case XP_PLUGIN_DRAIN_REWRITE:
                                           LOG_ERR("PEVT", "%s: outbox rewrite failed", sub.name);
                                           break;
                                         case XP_PLUGIN_DRAIN_FINISHED:
                                           LOG_DBG("PEVT", "%s: drained (stalled=%d)", sub.name, stalled);
                                           break;
                                       }
                                     }};
        xp_plugin_drain_lines(span(c.raw), budget, &lines);
      }};
  auto ops = opsCallbacks;
  ops.ctx = &context;
  xp_plugin_drain_file(budget, &ops);
}
}  // namespace
void drain(GfxRenderer* renderer, const size_t maxEvents) {
  xp_plugin_drain_all(subscribers, maxEvents, renderer, [](void* p, const Subscriber* sub, size_t* budget) {
    drainFile(*sub, static_cast<GfxRenderer*>(p), budget);
  });
}

}  // namespace pluginevents
