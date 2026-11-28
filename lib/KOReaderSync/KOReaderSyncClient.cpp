#include "KOReaderSyncClient.h"

#include <HalMemory.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <base64.h>

#include "KOReaderCredentialStore.h"
#include "SyncBridge.h"
#include "SyncJson.h"
int KOReaderSyncClient::lastHttpCode = 0;
namespace {
struct Session {
  const KOReaderProgress* input = nullptr;
  KOReaderProgress* output = nullptr;
  const std::string* document = nullptr;
  SyncJson json;
  std::optional<JsonDocument> doc, extra;
  std::optional<freeink::SecureHttpClient> http;
  std::string url, body, temporary;
  const char* error = "";
  size_t free = 0, block = 0;
};
XpSyncValue text(const std::string& s) { return {0, 0, 0, syncSpan(s), 1}; }
XpSyncValue number(uint32_t kind, int64_t n) { return {kind, n, 0, {nullptr, 0}, 1}; }
XpSyncValue real(float f, bool valid = true) { return {3, 0, f, {nullptr, 0}, valid}; }
XpSyncValue get(void* raw, uint32_t field) {
  auto& c = *static_cast<Session*>(raw);
  switch (static_cast<XpSyncProtocolField>(field)) {
    case XP_SYNC_PROTOCOL_USERNAME:
      return text(KOREADER_STORE.getUsername());
    case XP_SYNC_PROTOCOL_PASSWORD:
      return text(KOREADER_STORE.getPassword());
    case XP_SYNC_PROTOCOL_MD5:
      c.temporary = KOREADER_STORE.getMd5Password();
      return text(c.temporary);
    case XP_SYNC_PROTOCOL_BASIC: {
      const std::string credentials = KOREADER_STORE.getUsername() + ":" + KOREADER_STORE.getPassword();
      const String encoded = base64::encode(credentials.c_str());
      c.temporary = std::string("Basic ") + encoded.c_str();
      return text(c.temporary);
    }
    case XP_SYNC_PROTOCOL_HEADER0_NAME:
    case XP_SYNC_PROTOCOL_HEADER1_NAME:
      return text(KOREADER_STORE.getCustomHeaders()[(field - 4) / 2].name);
    case XP_SYNC_PROTOCOL_HEADER0_VALUE:
    case XP_SYNC_PROTOCOL_HEADER1_VALUE:
      return text(KOREADER_STORE.getCustomHeaders()[(field - 5) / 2].value);
    case XP_SYNC_PROTOCOL_SERVER_TYPE:
      return number(1, static_cast<uint8_t>(KOREADER_STORE.getServerType()));
    case XP_SYNC_PROTOCOL_DOCUMENT:
      return text(c.document ? *c.document : c.input->document);
    case XP_SYNC_PROTOCOL_PROGRESS:
      return text(c.input->progress);
    case XP_SYNC_PROTOCOL_PERCENTAGE:
      return real(c.input->percentage);
    case XP_SYNC_PROTOCOL_HAS_METADATA:
      return number(2, c.input->metadata.has_value());
    case XP_SYNC_PROTOCOL_FILENAME:
      return text(c.input->metadata->filename);
    case XP_SYNC_PROTOCOL_TITLE:
      return text(c.input->metadata->title);
    case XP_SYNC_PROTOCOL_AUTHORS:
      return text(c.input->metadata->authors);
    case XP_SYNC_PROTOCOL_EXTRA_JSON:
      return text(c.input->metadata->extraJson);
    case XP_SYNC_PROTOCOL_ISBN:
      return text(c.input->metadata->isbn);
    case XP_SYNC_PROTOCOL_ASIN:
      return text(c.input->metadata->asin);
    case XP_SYNC_PROTOCOL_SERIES:
      return text(c.input->metadata->series);
    case XP_SYNC_PROTOCOL_SERIES_INDEX:
      return real(c.input->metadata->seriesIndex.value_or(0), c.input->metadata->seriesIndex.has_value());
    case XP_SYNC_PROTOCOL_HAS_POSITION:
      return number(2, c.input->position.has_value());
    case XP_SYNC_PROTOCOL_PCT_Q:
      return number(5, c.input->position->pctQ);
    case XP_SYNC_PROTOCOL_SPINE:
      return number(6, c.input->position->spineIndex);
    case XP_SYNC_PROTOCOL_PAGE:
      return number(6, c.input->position->pageNumber);
    case XP_SYNC_PROTOCOL_PAGES:
      return number(6, c.input->position->totalPages);
    case XP_SYNC_PROTOCOL_PARAGRAPH: {
      auto v = number(6, c.input->position->paragraphIndex.value_or(0));
      v.valid = c.input->position->paragraphIndex.has_value();
      return v;
    }
    case XP_SYNC_PROTOCOL_XPATH:
      return text(c.input->position->xpath);
    default:
      return number(0, 0);
  }
}
void set(void* raw, uint32_t field, XpSyncValue v) {
  auto& p = *static_cast<Session*>(raw)->output;
  const auto string = [&]() { return std::string(reinterpret_cast<const char*>(v.text.data), v.text.len); };
  switch (static_cast<XpSyncProtocolField>(field)) {
    case XP_SYNC_PROTOCOL_DOCUMENT:
      p.document = string();
      break;
    case XP_SYNC_PROTOCOL_PROGRESS:
      p.progress = string();
      break;
    case XP_SYNC_PROTOCOL_PERCENTAGE:
      p.percentage = v.real;
      break;
    case XP_SYNC_PROTOCOL_DEVICE:
      p.device = string();
      break;
    case XP_SYNC_PROTOCOL_DEVICE_ID:
      p.deviceId = string();
      break;
    case XP_SYNC_PROTOCOL_TIMESTAMP:
      p.timestamp = v.number;
      break;
    case XP_SYNC_PROTOCOL_HAS_POSITION:
      if (v.number)
        p.position.emplace();
      else
        p.position.reset();
      break;
    case XP_SYNC_PROTOCOL_PCT_Q:
      p.position->pctQ = v.number;
      break;
    case XP_SYNC_PROTOCOL_SPINE:
      p.position->spineIndex = v.number;
      break;
    case XP_SYNC_PROTOCOL_PAGE:
      p.position->pageNumber = v.number;
      break;
    case XP_SYNC_PROTOCOL_PAGES:
      p.position->totalPages = v.number;
      break;
    case XP_SYNC_PROTOCOL_PARAGRAPH:
      p.position->paragraphIndex = v.number;
      break;
    case XP_SYNC_PROTOCOL_XPATH:
      p.position->xpath = string();
      break;
    default:
      break;  // Input-only model fields have no response setter.
  }
}
XpSyncValue call(void* raw, uint32_t op, XpSyncSpan key, XpSyncValue v) {
  auto& c = *static_cast<Session*>(raw);
  const auto string = [](XpSyncSpan s) { return std::string(reinterpret_cast<const char*>(s.data), s.len); };
  switch (static_cast<XpSyncTransportOp>(op)) {
    case XP_SYNC_HTTP_STATUS:
      KOReaderSyncClient::lastHttpCode = v.number;
      break;
    case XP_SYNC_HTTP_URL_RESET:
      c.url = KOREADER_STORE.getBaseUrl();
      break;
    case XP_SYNC_HTTP_URL_APPEND:
      c.url.append(reinterpret_cast<const char*>(key.data), key.len);
      break;
    case XP_SYNC_HTTP_HEAP: {
      const auto h = HalMemory::getDefaultHeap();
      c.free = h.freeBytes;
      c.block = h.largestBlockBytes;
      return {4, static_cast<int64_t>(c.free), static_cast<double>(c.block), {nullptr, 0}, 1};
    }
    case XP_SYNC_HTTP_INSECURE:
      c.http.emplace();
      c.http->setInsecure();
      break;
    case XP_SYNC_HTTP_BEGIN:
      return number(2, c.http->begin(c.url));
    case XP_SYNC_HTTP_HEADER:
      c.http->addHeader(string(key), string(v.text));
      std::string{}.swap(c.temporary);
      break;
    case XP_SYNC_HTTP_GET:
      return number(4, c.http->GET());
    case XP_SYNC_HTTP_POST:
      return number(4, c.http->sendRequest("POST", c.body));
    case XP_SYNC_HTTP_PUT:
      return number(4, c.http->sendRequest("PUT", c.body));
    case XP_SYNC_HTTP_END:
      c.http->end();
      break;
    case XP_SYNC_HTTP_JSON_RESET:
      c.doc.emplace();
      c.json.output[0] = c.doc->as<JsonVariant>();
      c.json.input[0] = c.json.output[0];
      break;
    case XP_SYNC_HTTP_PARSE_RESPONSE: {
      const auto error = deserializeJson(*c.doc, c.http->getString().c_str());
      c.error = error.c_str();
      c.json.input[0] = c.doc->as<JsonVariantConst>();
      return number(2, !error);
    }
    case XP_SYNC_HTTP_PARSE_EXTRA: {
      c.extra.emplace();
      const auto error = deserializeJson(*c.extra, string(v.text));
      c.json.input[3] = c.extra->as<JsonVariantConst>();
      return number(2, !error);
    }
    case XP_SYNC_HTTP_SERIALIZE:
      serializeJson(*c.doc, c.body);
      std::string{}.swap(c.temporary);
      break;
    case XP_SYNC_HTTP_RELEASE_EXTRA:
      c.json.input[3] = JsonVariantConst{};
      c.extra.reset();
      break;
  }
  return number(0, 0);
}
void diagnostic(void* raw, uint32_t event, int32_t value) {
  const auto& c = *static_cast<Session*>(raw);
  switch (static_cast<XpSyncHttpDiagnostic>(event)) {
    case XP_SYNC_HTTP_DIAG_UNUSED:
      break;
    case XP_SYNC_HTTP_DIAG_NO_CREDENTIALS:
      LOG_DBG("KOSync", "No credentials configured");
      break;
    case XP_SYNC_HTTP_DIAG_AUTH:
      LOG_DBG("KOSync", "Authenticating: %s (heap: %u)", c.url.c_str(), (unsigned)ESP.getFreeHeap());
      break;
    case XP_SYNC_HTTP_DIAG_CREATE:
      LOG_DBG("KOSync", "Creating account: %s (heap: %u)", c.url.c_str(), (unsigned)ESP.getFreeHeap());
      break;
    case XP_SYNC_HTTP_DIAG_GET:
      LOG_DBG("KOSync", "Getting progress: %s (heap: %u)", c.url.c_str(), (unsigned)ESP.getFreeHeap());
      break;
    case XP_SYNC_HTTP_DIAG_UPDATE:
      LOG_DBG("KOSync", "Updating progress: %s (heap: %u)", c.url.c_str(), (unsigned)ESP.getFreeHeap());
      break;
    case XP_SYNC_HTTP_DIAG_LOW_HEAP:
      LOG_ERR("KOSync",
              "Insufficient allocatable heap for TLS handshake: %zu bytes free (need %u), %zu max alloc (need %u)",
              c.free, 35000u, c.block, 20000u);
      break;
    case XP_SYNC_HTTP_DIAG_BAD_URL:
      LOG_ERR("KOSync", "Bad URL: %s", c.url.c_str());
      break;
    case XP_SYNC_HTTP_DIAG_AUTH_RESPONSE:
      LOG_DBG("KOSync", "Auth response: %d", value);
      break;
    case XP_SYNC_HTTP_DIAG_CREATE_RESPONSE:
      LOG_DBG("KOSync", "Create user response: %d", value);
      break;
    case XP_SYNC_HTTP_DIAG_GET_RESPONSE:
      LOG_DBG("KOSync", "Get progress response: %d", value);
      break;
    case XP_SYNC_HTTP_DIAG_JSON_ERROR:
      LOG_ERR("KOSync", "JSON parse failed: %s", c.error);
      break;
    case XP_SYNC_HTTP_DIAG_RICH_POSITION:
      LOG_DBG("KOSync", "Got rich position: spine=%u page=%u/%u para=%u", c.output->position->spineIndex,
              c.output->position->pageNumber, c.output->position->totalPages, static_cast<unsigned>(value));
      break;
    case XP_SYNC_HTTP_DIAG_PROGRESS:
      LOG_DBG("KOSync", "Got progress: %.2f%% at %s", c.output->percentage * 100, c.output->progress.c_str());
      break;
    case XP_SYNC_HTTP_DIAG_REQUEST_BODY:
      LOG_DBG("KOSync", "Request body: %s", c.body.c_str());
      break;
    case XP_SYNC_HTTP_DIAG_UPDATE_RESPONSE:
      LOG_DBG("KOSync", "Update progress response: %d", value);
      break;
  }
}
KOReaderSyncClient::Error run(uint32_t method, Session& session) {
  const auto json = session.json.api();
  const XpSyncModel model{&session, get, set};
  const XpSyncTransport transport{&session, call, diagnostic};
  return static_cast<KOReaderSyncClient::Error>(xp_sync_request(method, &json, &model, &transport));
}
}  // namespace
KOReaderSyncClient::Error KOReaderSyncClient::authenticate() {
  Session session;
  return run(0, session);
}
KOReaderSyncClient::Error KOReaderSyncClient::createUser() {
  Session session;
  return run(1, session);
}
KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& document, KOReaderProgress& out) {
  Session session;
  session.document = &document;
  session.output = &out;
  return run(2, session);
}
KOReaderSyncClient::Error KOReaderSyncClient::updateProgress(const KOReaderProgress& progress) {
  Session session;
  session.input = &progress;
  return run(3, session);
}
const char* KOReaderSyncClient::errorString(Error error) { return xp_sync_error_string(error); }
