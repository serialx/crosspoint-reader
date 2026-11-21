#include "network/HttpDownloader.h"

#include <Logging.h>
#include <SecureHttpClient.h>

#include <cstddef>

#include "network/HttpTransfer.h"
extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }
namespace {
static_assert(sizeof(int) == sizeof(int32_t) && alignof(int) == alignof(int32_t));
int32_t lastStatusValue = 0;
bool lastRedirectedValue = false;
struct Context {
  const HttpDownloader::DataCallback* sink = nullptr;
  const HttpDownloader::ProgressCallback* progress = nullptr;
  const std::vector<HttpDownloader::Header>* headers = nullptr;
};
void headerAt(void* p, size_t i, XpHttpNativeText* name, XpHttpNativeText* value) {
  auto& c = *static_cast<Context*>(p);
  *name = httptransfer::nativeText((*c.headers)[i].first);
  *value = httptransfer::nativeText((*c.headers)[i].second);
}
bool sink(void* p, XpHttpBytes bytes) { return (*static_cast<Context*>(p)->sink)(bytes.data, bytes.length); }
void progress(void* p, size_t bytes, size_t total) {
  auto& c = *static_cast<Context*>(p);
  if (c.progress && *c.progress) (*c.progress)(bytes, total);
}
struct Call {
  Context context;
  XpHttpConsumerRetireN5N6C2C4B2 consumer{&context, 0, headerAt, sink, progress};
  HalFile file;
  alignas(freeink::SecureHttpClient) std::byte client[sizeof(freeink::SecureHttpClient)];
  XpHttpRequestRetireN5N6C2C4B2 request{};
  Call(const std::string& url, XpHttpTransferKind kind) {
    request.kind = kind;
    request.url = httptransfer::nativeText(url);
    request.consumer = &consumer;
    request.client_storage = client;
    request.file_storage = &file;
    request.last_status = &lastStatusValue;
    request.last_redirected = &lastRedirectedValue;
  }
  void auth(const std::string& user, const std::string& pass, const std::vector<HttpDownloader::Header>& headers) {
    request.username = httptransfer::nativeText(user);
    request.password = httptransfer::nativeText(pass);
    context.headers = &headers;
    consumer.header_count = headers.size();
  }
  __attribute__((always_inline)) uint32_t run() {
    return xp_http_transfer_retire_n5_n6_c2_c4_b2(&request, &httptransfer::leaves());
  }
};
}  // namespace
int HttpDownloader::lastStatus() { return lastStatusValue; }
bool HttpDownloader::lastAnswerRedirected() { return lastRedirectedValue; }
bool HttpDownloader::fetchUrl(const std::string& url, Stream& out, const std::string& user, const std::string& pass,
                              const std::vector<Header>& headers) {
  return fetchUrl(url, [&out](const uint8_t* p, size_t n) { return out.write(p, n) == n; }, user, pass, headers);
}
bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& callback, const std::string& user,
                              const std::string& pass, const std::vector<Header>& headers) {
  Call c(url, XP_HTTP_TRANSFER_FETCH);
  c.context.sink = &callback;
  c.auth(user, pass, headers);
  return c.run() == OK;
}
bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& callback, const FetchOptions& options) {
  Call c(url, XP_HTTP_TRANSFER_CATALOG);
  c.context.sink = &callback;
  c.auth(options.username, options.password, options.headers);
  c.request.bearer = httptransfer::nativeText(options.bearer);
  c.request.accept = httptransfer::nativeText(options.accept);
  c.request.accept_language = httptransfer::nativeText(options.acceptLanguage);
  c.request.capture_error_body = options.captureErrorBody;
  c.request.status_output = options.statusOut;
  return c.run() == OK;
}
bool HttpDownloader::postForm(const std::string& url, const std::string& body, std::string& out, int* status) {
  Call c(url, XP_HTTP_TRANSFER_FORM);
  c.request.body = httptransfer::nativeText(body);
  auto response = httptransfer::mutableText(out);
  c.request.form_response = &response;
  c.request.status_output = status;
  return c.run() == OK;
}
HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& path,
                                                             ProgressCallback callback, const bool* cancel,
                                                             const std::string& user, const std::string& pass,
                                                             const std::vector<Header>& headers, bool downgrade,
                                                             FileOpen fileOpen) {
  Call c(url, XP_HTTP_TRANSFER_DOWNLOAD);
  c.context.progress = &callback;
  c.auth(user, pass, headers);
  c.request.destination = httptransfer::nativeText(path);
  c.request.cancel_flag = cancel;
  c.request.downgrade_redirects = downgrade;
  c.request.defer_open = fileOpen != FileOpen::Immediate;
  c.request.refuse_write = fileOpen == FileOpen::RefuseOnSuccess;
  return static_cast<DownloadError>(c.run());
}
HttpDownloader::DownloadError HttpDownloader::postToFile(const std::string& url, const std::string& body,
                                                         const std::string& type, const std::string& path,
                                                         ProgressCallback callback, const bool* cancel) {
  Call c(url, XP_HTTP_TRANSFER_POST_FILE);
  c.context.progress = &callback;
  c.request.body = httptransfer::nativeText(body);
  c.request.content_type = httptransfer::nativeText(type);
  c.request.destination = httptransfer::nativeText(path);
  c.request.cancel_flag = cancel;
  return static_cast<DownloadError>(c.run());
}
