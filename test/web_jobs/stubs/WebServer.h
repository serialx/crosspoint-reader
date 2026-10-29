#pragma once
#include "Arduino.h"
enum HTTPMethod {
  HTTP_ANY,
  HTTP_GET,
  HTTP_HEAD,
  HTTP_POST,
  HTTP_PUT,
  HTTP_PATCH,
  HTTP_DELETE,
  HTTP_OPTIONS,
  HTTP_PROPFIND,
  HTTP_LOCK,
  HTTP_UNLOCK,
  HTTP_MKCOL,
  HTTP_MOVE,
  HTTP_COPY
};
enum { RAW_START, RAW_WRITE, RAW_END, RAW_ABORTED };
struct HTTPRaw {
  int status;
  uint8_t* buf = nullptr;
  size_t currentSize = 0;
  unsigned totalSize = 0;
};
constexpr size_t CONTENT_LENGTH_UNKNOWN = size_t(-1);
class WebServer {
 public:
  String path = "/book.epub";
  HTTPMethod verb = HTTP_GET;
  std::map<std::string, String> headers;
  String uri() const { return path; }
  HTTPMethod method() const { return verb; }
  String header(const char* k) {
    auto it = headers.find(k);
    return it == headers.end() ? String{} : it->second;
  }
  size_t clientContentLength() const { return oracle::fault == "body" ? 1 : 0; }
  NetworkClient client() { return {}; }
  void send(int code, const char* mime = "text/html", const String& body = "") {
    oracle::record("send %d %s %s", code, mime, body.c_str());
  }
  void sendHeader(const char* k, const char* v) { oracle::record("response-header %s %s", k, v); }
  void setContentLength(size_t n) { oracle::record("length %zu", n); }
  void sendContent(const char* p, size_t n) { sendContent(String(p, n)); }
  void sendContent(const String& s) { oracle::record("content %s", s.c_str()); }
  static String urlDecode(const String& s) {
    String out;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '%' && i + 2 < s.size()) {
        char b[3] = {s[i + 1], s[i + 2], 0};
        out += char(strtol(b, nullptr, 16));
        i += 2;
      } else
        out += s[i] == '+' ? ' ' : s[i];
    }
    return out;
  }
};
class RequestHandler {
 public:
  virtual ~RequestHandler() = default;
  virtual bool canHandle(WebServer&, HTTPMethod, const String&) { return false; }
  virtual bool canRaw(WebServer&, const String&) { return false; }
  virtual void raw(WebServer&, const String&, HTTPRaw&) {}
  virtual bool handle(WebServer&, HTTPMethod, const String&) { return false; }
};
