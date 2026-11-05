#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <ResumableFetch.h>
#include <xp_ffi.h>

#include <optional>

#include "WifiPowerSaveGuard.h"

extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }
namespace {
int g_lastStatus = 0;
bool g_lastRedirected = false;
void setup(freeink::SecureHttpClient& http) {
  http.setTimeout(60000);
  http.setInsecure();
  http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
}
struct Context {
  explicit Context(const std::string& source) : url(source) {}
  const std::string& url;
  const HttpDownloader::DataCallback* sink = nullptr;
  const HttpDownloader::FetchOptions* options = nullptr;
  const std::string* username = nullptr;
  const std::string* password = nullptr;
  const std::vector<HttpDownloader::Header>* headers = nullptr;
  const std::string* body = nullptr;
  const std::string* contentType = nullptr;
  const std::string* destination = nullptr;
  std::string* response = nullptr;
  const HttpDownloader::ProgressCallback* progress = nullptr;
  const bool* cancel = nullptr;
  bool downgrade = false;
  std::string current;
  std::string origin;
  std::string location;
  std::string part;
  HalFile file;
  std::optional<WifiPowerSaveGuard> guard;
};
size_t operation(void* opaque, uint32_t op, size_t a, size_t b, const uint8_t* bytes) {
  auto& c = *static_cast<Context*>(opaque);
  switch (op) {
    case XP_HTTP_REMOVE:
      return Storage.remove(c.part.c_str());
    case XP_HTTP_OPEN:
      return Storage.openFileForWrite("HTTP", c.part.c_str(), c.file);
    case XP_HTTP_CLOSE:
      if (c.file.isOpen()) c.file.close();
      return 0;
    case XP_HTTP_REPLACE:
      return Storage.replaceFile(c.part.c_str(), c.destination->c_str());
    case XP_HTTP_WRITE:
      return c.file.write(bytes, a);
    case XP_HTTP_PROGRESS:
      if (c.progress && *c.progress) (*c.progress)(a, b);
      return 0;
    case XP_HTTP_SINK:
      return (*c.sink)(bytes, a);
    case XP_HTTP_APPEND:
      c.response->append(reinterpret_cast<const char*>(bytes), a);
      return 0;
    case XP_HTTP_RESOLVE: {
      const bool resolved =
          !c.location.empty() && freeink::SecureHttpClient::resolveUrl(c.current, c.location, c.current);
      std::string{}.swap(c.location);
      return resolved;
    }
    case XP_HTTP_SAME_ORIGIN:
      return c.options ? freeink::fetchOrigin(c.current) == c.origin : true;
    case XP_HTTP_LAST_STATUS:
      g_lastStatus = static_cast<int>(a);
      return 0;
    case XP_HTTP_LAST_REDIRECTED:
      g_lastRedirected = a;
      return 0;
    case XP_HTTP_GUARD_ENTER:
      c.guard.emplace();
      return 0;
    case XP_HTTP_GUARD_EXIT:
      c.guard.reset();
      return 0;
  }
  return 0;
}
void diagnostic(void* opaque, uint32_t event, size_t a, size_t b) {
  const auto& c = *static_cast<Context*>(opaque);
  switch (event) {
    case 0:
      LOG_ERR("HTTP", "wolfSSL request unauthorized: status %d: %s", (int)a, c.url.c_str());
      break;
    case 1:
      LOG_ERR("HTTP", "wolfSSL request failed: status %d: %s", (int)a, c.url.c_str());
      break;
    case 2:
      LOG_ERR("HTTP", "wolfSSL incomplete: got %zu of %zu bytes", a, b);
      break;
    case 3:
      LOG_ERR("HTTP", "Failed to open file for writing");
      break;
    case 4:
      LOG_ERR("HTTP", "POST failed: status %d: %s", (int)a, c.url.c_str());
      break;
    case 5:
      LOG_ERR("HTTP", "no data received");
      break;
    case 6:
      LOG_ERR("HTTP", "POST response incomplete: %zu of %zu bytes", a, b);
      break;
    case 7:
      LOG_ERR("HTTP", "Failed to move download into place: %s", c.destination->c_str());
      break;
    case 8:
      LOG_DBG("HTTP", "Downloaded %zu bytes", a);
      break;
  }
}
int32_t request(void* opaque, XpHttpState* state);
XpHttpServices services(Context& c) { return {&c, operation, request, diagnostic}; }
int32_t request(void* opaque, XpHttpState* state) {
  auto& c = *static_cast<Context*>(opaque);
  const auto api = services(c);
  if (state->mode == XP_HTTP_FETCH || state->mode == XP_HTTP_DOWNLOAD) {
    freeink::FetchOptions options;
    options.redirectToHttp = c.downgrade;
    freeink::FetchSink sink;
    sink.write = [&](const uint8_t* data, size_t len) { return xp_http_chunk(&api, state, data, len); };
    if (state->mode == XP_HTTP_DOWNLOAD) {
      sink.rewind = [&] {
        c.file.close();
        return Storage.openFileForWrite("HTTP", c.part.c_str(), c.file);
      };
      sink.progress = *c.progress;
    }
    const auto result = freeink::fetchResumable(
        c.url, options,
        [&](freeink::SecureHttpClient& http, bool sameOrigin) {
          state->same_origin = sameOrigin;
          setup(http);
          if (sameOrigin) {
            if (xp_http_auth(sameOrigin, 0, c.username->size(), c.password->size()) == 1)
              http.setBasicAuth(*c.username, *c.password);
            for (const auto& h : *c.headers) http.addHeader(h.first, h.second);
          }
          LOG_DBG("HTTP", "wolfSSL GET: %s (heap %u, max block %u)", c.url.c_str(), (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
        },
        sink, [&] { return c.cancel && *c.cancel; });
    state->bytes = result.bytes;
    state->total = result.total;
    state->aborted = result.aborted;
    state->stopped = result.stopped;
    state->complete = result.complete;
    return result.status;
  }
  freeink::SecureHttpClient http;
  const std::string& url = c.options ? c.current : c.url;
  if (!http.begin(url)) {
    LOG_ERR("HTTP", "bad URL: %s", url.c_str());
    return state->mode == XP_HTTP_POST_FILE ? INT32_MIN : 0;
  }
  setup(http);
  if (c.options) {
    const auto& o = *c.options;
    if (state->same_origin) {
      switch (xp_http_auth(state->same_origin, o.bearer.size(), o.username.size(), o.password.size())) {
        case 1:
          http.setBasicAuth(o.username, o.password);
          break;
        case 2:
          http.addHeader("Authorization", "Bearer " + o.bearer);
          break;
        default:
          break;
      }
      for (const auto& h : o.headers) http.addHeader(h.first, h.second);
    }
    if (!o.accept.empty()) http.addHeader("Accept", o.accept);
    if (!o.acceptLanguage.empty()) http.addHeader("Accept-Language", o.acceptLanguage);
    LOG_DBG("HTTP", "wolfSSL GET: %s", c.current.c_str());
  } else {
    if (state->mode == XP_HTTP_FORM)
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    else
      http.addHeader("Content-Type", *c.contentType);
    if (state->mode == XP_HTTP_FORM) {
      LOG_DBG("HTTP", "wolfSSL POST: %s", c.url.c_str());
    }
  }
  const auto sink = [&](const uint8_t* data, size_t len) {
    state->status = http.getStatus();
    state->known = http.hasContentLength();
    state->total = http.getContentLength();
    return xp_http_chunk(&api, state, data, len);
  };
  const int result = c.options ? http.GET(sink)
                               : http.sendRequest("POST", reinterpret_cast<const uint8_t*>(c.body->data()),
                                                  c.body->size(), sink, [&] { return c.cancel && *c.cancel; });
  if (c.options && xp_http_redirect(result)) c.location = http.getHeader("location");
  state->aborted = http.aborted();
  state->known = http.hasContentLength();
  state->total = http.getContentLength();
  state->complete = true;
  return result;
}
uint32_t run(Context& c, XpHttpMode mode, int* status = nullptr) {
  XpHttpState state{};
  state.mode = mode;
  state.capture = c.options && c.options->captureErrorBody;
  const auto api = services(c);
  const uint32_t result = xp_http_run(&api, &state);
  if (status) *status = state.status > 0 ? state.status : 0;
  return result;
}
}  // namespace
int HttpDownloader::lastStatus() { return g_lastStatus; }
bool HttpDownloader::lastAnswerRedirected() { return g_lastRedirected; }
bool HttpDownloader::fetchUrl(const std::string& url, Stream& out, const std::string& user, const std::string& password,
                              const std::vector<Header>& headers) {
  return fetchUrl(url, [&out](const uint8_t* p, size_t n) { return out.write(p, n) == n; }, user, password, headers);
}
bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& sink, const std::string& user,
                              const std::string& password, const std::vector<Header>& headers) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  Context c{url};
  c.sink = &sink;
  c.username = &user;
  c.password = &password;
  c.headers = &headers;
  return run(c, XP_HTTP_FETCH) == OK;
}
bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& sink, const FetchOptions& options) {
  WifiPowerSaveGuard guard;
  Context c{url};
  c.sink = &sink;
  c.options = &options;
  c.current = url;
  c.origin = freeink::fetchOrigin(url);
  return run(c, XP_HTTP_CATALOG, options.statusOut) == OK;
}
bool HttpDownloader::postForm(const std::string& url, const std::string& body, std::string& out, int* status) {
  WifiPowerSaveGuard guard;
  out.clear();
  Context c{url};
  c.body = &body;
  c.response = &out;
  return run(c, XP_HTTP_FORM, status) == OK;
}
HttpDownloader::DownloadError HttpDownloader::postToFile(const std::string& url, const std::string& body,
                                                         const std::string& type, const std::string& path,
                                                         ProgressCallback progress, const bool* cancel) {
  LOG_DBG("HTTP", "POST download: %s -> %s", url.c_str(), path.c_str());
  WifiPowerSaveGuard guard;
  Context c{url};
  c.body = &body;
  c.contentType = &type;
  c.destination = &path;
  c.part = path + ".part";
  c.progress = &progress;
  c.cancel = cancel;
  return static_cast<DownloadError>(run(c, XP_HTTP_POST_FILE));
}
HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& path,
                                                             ProgressCallback progress, const bool* cancel,
                                                             const std::string& user, const std::string& password,
                                                             const std::vector<Header>& headers, bool downgrade) {
  LOG_DBG("HTTP", "Downloading: %s -> %s", url.c_str(), path.c_str());
  Context c{url};
  c.destination = &path;
  c.part = path + ".part";
  c.progress = &progress;
  c.cancel = cancel;
  c.username = &user;
  c.password = &password;
  c.headers = &headers;
  c.downgrade = downgrade;
  return static_cast<DownloadError>(run(c, XP_HTTP_DOWNLOAD));
}
