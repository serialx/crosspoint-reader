#include "KOReaderCredentialStore.h"

#include <Logging.h>
#include <MD5Builder.h>
#include <ObfuscationUtils.h>

#include "SyncBridge.h"
#include "SyncJson.h"
namespace {
struct Context {
  KOReaderCredentialStore* store;
  std::string password;
  std::string base;
};
XpSyncValue text(const std::string& s) { return {0, 0, 0, syncSpan(s), 1}; }
XpSyncValue number(uint32_t kind, int64_t n) { return {kind, n, 0, {nullptr, 0}, 1}; }
XpSyncValue get(void* raw, uint32_t field) {
  auto& c = *static_cast<Context*>(raw);
  const auto& s = *c.store;
  switch (static_cast<XpSyncStoreField>(field)) {
    case XP_SYNC_STORE_USERNAME:
      return text(s.getUsername());
    case XP_SYNC_STORE_PASSWORD:
      return text(s.getPassword());
    case XP_SYNC_STORE_SERVER_URL:
      return text(s.getServerUrl());
    case XP_SYNC_STORE_SERVER_TYPE:
      return number(1, static_cast<uint8_t>(s.getServerType()));
    case XP_SYNC_STORE_MATCH_METHOD:
      return number(1, static_cast<uint8_t>(s.getMatchMethod()));
    case XP_SYNC_STORE_SEND_METADATA:
      return number(2, s.getSendMetadata());
    case XP_SYNC_STORE_SYNC_CLIPPINGS:
      return number(2, s.getSyncClippings());
    case XP_SYNC_STORE_BEHAVIOR:
      return number(1, static_cast<uint8_t>(s.getSyncBehavior()));
    case XP_SYNC_STORE_HEADER0_NAME:
    case XP_SYNC_STORE_HEADER1_NAME:
      return text(s.getCustomHeaders()[(field - 8) / 2].name);
    case XP_SYNC_STORE_HEADER0_VALUE:
    case XP_SYNC_STORE_HEADER1_VALUE:
      return text(s.getCustomHeaders()[(field - 9) / 2].value);
    case XP_SYNC_STORE_EXTRACTED_PASSWORD:
      return text(c.password);
    case XP_SYNC_STORE_BASE_URL:
      c.base = s.getBaseUrl();
      return text(c.base);
    default:
      return number(0, 0);
  }
}
void diagnostic(void*, uint32_t event, uint32_t value) {
  switch (static_cast<XpSyncStoreDiagnostic>(event)) {
    case XP_SYNC_STORE_DIAG_UNUSED:
      break;
    case XP_SYNC_STORE_DIAG_PIN_LEGACY:
      LOG_DBG("KRS", "Pre-v2 config used the old default server; pinning %s", "https://sync.koreader.rocks:443");
      break;
    case XP_SYNC_STORE_DIAG_BAD_SERVER_TYPE:
      LOG_DBG("KRS", "Invalid serverType %u in JSON, resetting to KOSYNC", value);
      break;
    case XP_SYNC_STORE_DIAG_BAD_MATCH_METHOD:
      LOG_DBG("KRS", "Invalid matchMethod %u in JSON, resetting to FILENAME", value);
      break;
    case XP_SYNC_STORE_DIAG_BAD_BEHAVIOR:
      LOG_DBG("KRS", "Invalid syncBehavior %u in JSON, resetting to ASK_EVERY_TIME", value);
      break;
  }
}
}  // namespace
void KOReaderCredentialStore::toJson(JsonDocument& doc) const {
  Context context{const_cast<KOReaderCredentialStore*>(this), {}, {}};
  SyncJson json;
  json.output[0] = doc.as<JsonVariant>();
  json.input[0] = json.output[0];
  const auto api = json.api();
  const XpSyncModel model{&context, get, [](void*, uint32_t, XpSyncValue) {}};
  xp_sync_store_save(&api, &model);
}
bool KOReaderCredentialStore::fromJson(JsonVariantConst doc) {
  bool resave = false;
  Context context{this, extractPassword(doc, resave), {}};
  SyncJson json;
  json.input[0] = doc;
  const auto api = json.api();
  const XpSyncModel model{
      &context, get, [](void* raw, uint32_t field, XpSyncValue v) {
        auto& c = *static_cast<Context*>(raw);
        auto& s = *c.store;
        const auto string = [&]() { return std::string(reinterpret_cast<const char*>(v.text.data), v.text.len); };
        switch (static_cast<XpSyncStoreField>(field)) {
          case XP_SYNC_STORE_USERNAME:
            s.username.assign(reinterpret_cast<const char*>(v.text.data), v.text.len);
            break;
          case XP_SYNC_STORE_PASSWORD:
            s.password.assign(reinterpret_cast<const char*>(v.text.data), v.text.len);
            LOG_DBG("KRS", "Set credentials for user: %s", s.username.c_str());
            break;
          case XP_SYNC_STORE_SERVER_URL:
            s.setServerUrl(string());
            break;
          case XP_SYNC_STORE_SERVER_TYPE:
            s.setServerType(static_cast<KOReaderServerType>(v.number));
            break;
          case XP_SYNC_STORE_MATCH_METHOD:
            s.setMatchMethod(static_cast<DocumentMatchMethod>(v.number));
            break;
          case XP_SYNC_STORE_SEND_METADATA:
            s.setSendMetadata(v.number != 0);
            break;
          case XP_SYNC_STORE_SYNC_CLIPPINGS:
            s.setSyncClippings(v.number != 0);
            break;
          case XP_SYNC_STORE_BEHAVIOR:
            s.setSyncBehavior(static_cast<KOReaderSyncBehavior>(v.number));
            break;
          case XP_SYNC_STORE_HEADER0_NAME:
          case XP_SYNC_STORE_HEADER1_NAME:
            s.customHeaders[(field - 8) / 2].name.assign(reinterpret_cast<const char*>(v.text.data), v.text.len);
            break;
          case XP_SYNC_STORE_HEADER0_VALUE:
          case XP_SYNC_STORE_HEADER1_VALUE:
            s.customHeaders[(field - 9) / 2].value =
                obfuscation::deobfuscateFromBase64(reinterpret_cast<const char*>(v.text.data));
            break;
          case XP_SYNC_STORE_EXTRACTED_PASSWORD:
          case XP_SYNC_STORE_BASE_URL:
            break;
        }
      }};
  if (xp_sync_store_load(&api, &model, resave, diagnostic)) {
    LOG_DBG("KRS", "Resaving KOReader credentials to update format");
    requestResave();
  }
  return true;
}
void KOReaderCredentialStore::setCredentials(const std::string& user, const std::string& pass) {
  username = user;
  password = pass;
  LOG_DBG("KRS", "Set credentials for user: %s", user.c_str());
}

std::string KOReaderCredentialStore::getMd5Password() const {
  if (password.empty()) {
    return "";
  }

  // Calculate MD5 hash of password using ESP32's MD5Builder
  MD5Builder md5;
  md5.begin();
  md5.add(password.c_str());
  md5.calculate();

  return md5.toString().c_str();
}

bool KOReaderCredentialStore::hasCredentials() const { return !username.empty() && !password.empty(); }

void KOReaderCredentialStore::clearCredentials() {
  username.clear();
  password.clear();
  saveToFile();
  LOG_DBG("KRS", "Cleared KOReader credentials");
}

void KOReaderCredentialStore::setServerUrl(const std::string& url) {
  serverUrl = url;
  LOG_DBG("KRS", "Set server URL: %s", url.empty() ? "(default)" : url.c_str());
}

std::string KOReaderCredentialStore::getBaseUrl() const {
  uint8_t prefix = 0;
  const auto span = xp_sync_base_url(syncSpan(serverUrl), &prefix);
  std::string result = prefix ? "http://" : "";
  result.append(reinterpret_cast<const char*>(span.data), span.len);
  return result;
}

void KOReaderCredentialStore::setServerType(KOReaderServerType type) {
  type = static_cast<KOReaderServerType>(xp_sync_server_type(static_cast<uint8_t>(type)));
  serverType = type;
  LOG_DBG("KRS", "Set server type: %u", static_cast<unsigned>(type));
}

void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod method) {
  matchMethod = method;
  LOG_DBG("KRS", "Set match method: %s", method == DocumentMatchMethod::FILENAME ? "Filename" : "Binary");
}

void KOReaderCredentialStore::setSendMetadata(bool enabled) {
  sendMetadata = enabled;
  LOG_DBG("KRS", "Set send metadata: %s", enabled ? "true" : "false");
}

void KOReaderCredentialStore::setSyncBehavior(KOReaderSyncBehavior behavior) {
  behavior = static_cast<KOReaderSyncBehavior>(xp_sync_behavior(static_cast<uint8_t>(behavior)));
  syncBehavior = behavior;
  LOG_DBG("KRS", "Set sync behavior: %s", behavior == KOReaderSyncBehavior::SMART ? "Smart" : "Ask");
}

void KOReaderCredentialStore::setCustomHeader(size_t index, const std::string& name, const std::string& value) {
  if (index >= MAX_CUSTOM_HEADERS) return;
  customHeaders[index].name = name;
  customHeaders[index].value = value;
  LOG_DBG("KRS", "Set custom header %zu: %s", index, name.empty() ? "(cleared)" : name.c_str());
}
