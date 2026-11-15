#pragma once
#include <HalStorage.h>
#include <WebServer.h>
#include <xp_ffi.h>
class WebDAVHandler : public RequestHandler {
 public:
  bool canHandle(WebServer& server, HTTPMethod method, const String& uri) override;
  bool canRaw(WebServer& server, const String& uri) override;
  void raw(WebServer& server, const String& uri, HTTPRaw& raw) override;
  bool handle(WebServer& server, HTTPMethod method, const String& uri) override;

 private:
  HalFile putFile;
  String putPath;
  XpDavState putState{};
};
