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
constexpr size_t MAX_OUTBOX_BYTES = XP_PLUGIN_MAX_OUTBOX_BYTES;
constexpr size_t MAX_MANIFEST_SIZE = 8 * 1024;
constexpr size_t MAX_EVENT_LINE = XP_PLUGIN_MAX_EVENT_LINE;
// Event handler responses are acknowledgements, not content.
constexpr size_t MAX_EVENT_RESPONSE = 8 * 1024;

// Static subscription table: one slot per installed plugin that declares an
// "events" section. Rebuilt by refreshSubscriptions(); sized for a full
// plugin list screen, not a marketplace.
constexpr size_t MAX_EVENT_PLUGINS = XP_PLUGIN_MAX_EVENT_PLUGINS;
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
  const XpPluginOps ops{&context, [](void* p, uint32_t operation, uint64_t argument) -> uint64_t {
                          auto& c = *static_cast<AppendContext*>(p);
                          switch (static_cast<XpPluginAppendOp>(operation)) {
                            case XP_PLUGIN_FILE_OPEN:
                              c.file = Storage.open(c.path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
                              return c.file && c.file.isOpen();
                            case XP_PLUGIN_FILE_CLOSE:
                              c.file.close();
                              break;
                            case XP_PLUGIN_FILE_REMOVE:
                              Storage.remove(c.path.c_str());
                              break;
                            case XP_PLUGIN_FILE_SIZE:
                              return c.file.fileSize64();
                            case XP_PLUGIN_FILE_WRITE:
                              return c.file.write(reinterpret_cast<const uint8_t*>(c.line.data()), c.line.size());
                            case XP_PLUGIN_FILE_TRUNCATE:
                              c.file.truncate(argument);
                              break;
                            case XP_PLUGIN_FILE_FLUSH:
                              c.file.flush();
                              break;
                            case XP_PLUGIN_FILE_OPEN_ERROR:
                              LOG_ERR("PEVT", "%s: outbox open failed", c.sub.name);
                              break;
                            case XP_PLUGIN_FILE_CAP:
                              LOG_DBG("PEVT", "%s: outbox over cap, dropped", c.sub.name);
                              break;
                            case XP_PLUGIN_FILE_SHORT:
                              LOG_ERR("PEVT", "%s: short outbox append; event dropped", c.sub.name);
                              break;
                          }
                          return 0;
                        }};
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
        std::string* fields[] = {&c.out.tokenFile,     &c.out.tokenPath, &c.out.configFile, &c.out.authType,
                                 &c.out.authTokenPath, &c.handler.toast, &c.handler.dest};
        if (field == XP_PLUGIN_TOKEN_FILE || field == XP_PLUGIN_CONFIG_FILE)
          *fields[field] = pluginhttp::inPluginDir(c.sub.dir, value.len ? text : "");
        else
          fields[field]->assign(text, value.len);
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

  struct DeliveryContext {
    const DrainManifest& mf;
    const DrainManifest::Handler& handler;
    std::string& token;
    const pluginhttp::Headers& config;
    const pluginhttp::Headers& meta;
    JsonVariantConst vars;
    long long ts;
    const char* id;
    GfxRenderer* renderer;
    pluginhttp::Headers headers;
    std::string dest, minted;
    std::string substituted(const std::string& text) const {
      return drainSubstituted(text, token, config, meta, vars, ts, id);
    }
    void prepare(const pluginhttp::RequestSpec& req, pluginhttp::Headers& out) const {
      out.reserve(req.headers.size());
      for (const auto& h : req.headers) out.emplace_back(h.first, substituted(h.second));
    }
  } context{mf, *handler, token, config, meta, vars, ts, id, renderer, {}, {}, {}};
  const XpPluginOps ops{
      &context, [](void* p, uint32_t operation, uint64_t) -> uint64_t {
        auto& c = *static_cast<DeliveryContext*>(p);
        switch (static_cast<XpPluginDeliveryOp>(operation)) {
          case XP_PLUGIN_DELIVERY_PREPARE:
            c.prepare(c.handler.req, c.headers);
            break;
          case XP_PLUGIN_DELIVERY_ALLOWED:
            c.dest = c.substituted(c.handler.dest);
            return protectedpaths::isPluginPath(c.dest);
          case XP_PLUGIN_DELIVERY_DOWNLOAD: {
            const std::string tmp = c.dest + ".part";
            const int status =
                pluginhttp::requestToFile(nullptr, c.substituted(c.handler.req.url), c.handler.req.method,
                                          c.substituted(c.handler.req.body), c.headers, tmp.c_str(), 1024 * 1024);
            struct InstallContext {
              const std::string& dest;
              const std::string& tmp;
              std::string bak;
            } install{c.dest, tmp, {}};
            const XpPluginOps fileOps{&install, [](void* p, uint32_t operation, uint64_t) -> uint64_t {
                                        auto& c = *static_cast<InstallContext*>(p);
                                        switch (static_cast<XpPluginInstallOp>(operation)) {
                                          case XP_PLUGIN_INSTALL_EXISTS:
                                            return Storage.exists(c.dest.c_str());
                                          case XP_PLUGIN_INSTALL_REMOVE_BACKUP:
                                            if (c.bak.empty()) c.bak = c.dest + ".bak";
                                            Storage.remove(c.bak.c_str());
                                            break;
                                          case XP_PLUGIN_INSTALL_BACKUP:
                                            return Storage.rename(c.dest.c_str(), c.bak.c_str());
                                          case XP_PLUGIN_INSTALL_RENAME:
                                            return Storage.rename(c.tmp.c_str(), c.dest.c_str());
                                          case XP_PLUGIN_INSTALL_RESTORE:
                                            return Storage.rename(c.bak.c_str(), c.dest.c_str());
                                          case XP_PLUGIN_INSTALL_REMOVE_TEMP:
                                            Storage.remove(c.tmp.c_str());
                                            break;
                                          case XP_PLUGIN_INSTALL_COMMIT:
                                            if (library::isLibraryBookFile(c.dest)) library::markLibraryIndexDirty();
                                            break;
                                          case XP_PLUGIN_INSTALL_RESTORE_ERROR:
                                            LOG_ERR("PEVT", "restore of %s failed", c.dest.c_str());
                                            break;
                                        }
                                        return 0;
                                      }};
            return xp_plugin_install_download(status, &fileOps);
          }
          case XP_PLUGIN_DELIVERY_RESPONSE: {
            String body;
            return pluginhttp::request(nullptr, c.substituted(c.handler.req.url), c.handler.req.method,
                                       c.substituted(c.handler.req.body), c.headers, body, MAX_EVENT_RESPONSE);
          }
          case XP_PLUGIN_DELIVERY_RELEASE:
            pluginhttp::Headers{}.swap(c.headers);
            std::string{}.swap(c.dest);
            break;
          case XP_PLUGIN_DELIVERY_REJECTED:
            LOG_ERR("PEVT", "unsafe download dest rejected: %s", c.dest.c_str());
            break;
          case XP_PLUGIN_DELIVERY_MINT: {
            pluginhttp::Headers authHeaders;
            c.prepare(c.mf.authReq, authHeaders);
            return pluginhttp::mintPasswordToken(nullptr, c.substituted(c.mf.authReq.url), c.mf.authReq.method,
                                                 c.substituted(c.mf.authReq.body), authHeaders, c.mf.authTokenPath,
                                                 c.minted);
          }
          case XP_PLUGIN_DELIVERY_SAVE:
            pluginhttp::saveTokenToFile(c.mf.tokenFile, c.mf.tokenPath, c.minted);
            break;
          case XP_PLUGIN_DELIVERY_ACCEPT_TOKEN:
            c.token = c.minted;
            std::string{}.swap(c.minted);
            break;
          case XP_PLUGIN_DELIVERY_TOAST:
            GUI.drawPopup(*c.renderer, c.substituted(c.handler.toast).c_str());
            break;
        }
        return 0;
      }};
  return xp_plugin_deliver(mf.hasPasswordGrant(), handler->isDownload(), renderer && !handler->toast.empty(), &ops);
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
