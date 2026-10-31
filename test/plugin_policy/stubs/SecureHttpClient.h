#pragma once
#include "Oracle.h"
namespace oracle {
struct Response {
  int status = 200;
  std::vector<std::string> chunks = {"abc", "def"};
  size_t length = 6;
  bool known = true, complete = true;
  std::string location, range;
  unsigned delayMs = 0;
};
inline std::vector<Response> responses;
inline size_t responseIndex = 0;
}  // namespace oracle
namespace freeink {
class SecureHttpClient {
  oracle::Response response;
  bool cancelled = false;

 public:
#ifdef PLUGIN_TOKEN_LIFETIME
  ~SecureHttpClient() {
    if (oracle::responseIndex == 3) allocation_probe::beginToast();
  }
#endif
  using DataCallback = std::function<bool(const uint8_t*, size_t)>;
  using AbortCallback = std::function<bool()>;
  bool begin(const std::string& url) {
    oracle::record("begin %s", url.c_str());
    return url.find("://") != std::string::npos;
  }
  void setTimeout(unsigned n) { oracle::record("timeout %u", n); }
  void setInsecure() { oracle::record("insecure"); }
  void setUserAgent(const std::string& s) { oracle::record("agent %s", s.c_str()); }
  void setBasicAuth(const std::string& u, const std::string& p) { oracle::record("basic %s %s", u.c_str(), p.c_str()); }
  void addHeader(const std::string& k, const std::string& v) {
    oracle::record("header %s %s", k.c_str(), v.c_str());
#ifdef PLUGIN_TOKEN_LIFETIME
    oracle::record("header-bytes %s %zu %u", k.c_str(), v.size(),
                   oracle::hash(reinterpret_cast<const uint8_t*>(v.data()), v.size()));
#endif
  }
  int GET(const DataCallback& cb, const AbortCallback& abort = nullptr) {
    return sendRequest("GET", nullptr, 0, cb, abort);
  }
  int sendRequest(const char* m, const uint8_t* p, size_t n, const DataCallback& cb,
                  const AbortCallback& abort = nullptr) {
    oracle::record("request %s %zu %u", m, n, oracle::hash(p, n));
    if (oracle::responseIndex < oracle::responses.size())
      response = oracle::responses[oracle::responseIndex++];
    else {
      response.status = -1;
      response.chunks.clear();
    }
    if (response.delayMs) oracle::record("transport-delay %u", response.delayMs);
    for (const auto& c : response.chunks) {
      if (abort && abort()) {
        cancelled = true;
        break;
      }
      if (!cb(reinterpret_cast<const uint8_t*>(c.data()), c.size())) break;
    }
    return response.status;
  }
  std::vector<std::pair<std::string, std::string>> getHeaders() const { return {{"x-test", "yes"}}; }
  int getStatus() const { return response.status; }
  bool hasContentLength() const { return response.known; }
  size_t getContentLength() const { return response.length; }
  bool responseComplete() const { return response.complete; }
  bool aborted() const { return cancelled; }
  std::string getHeader(const std::string& s) const { return s == "location" ? response.location : response.range; }
  static bool resolveUrl(const std::string& base, const std::string& location, std::string& out) {
    if (location.find("://") != std::string::npos)
      out = location;
    else {
      size_t end = base.find('/', base.find("://") + 3);
      out = base.substr(0, end) + location;
    }
    return true;
  }
};
}  // namespace freeink
