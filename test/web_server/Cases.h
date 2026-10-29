#pragma once

#ifdef XP_FFI_HOST_FAULT_INJECTION
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();
#endif

static void record(const char* name, const std::function<void(CrossPointWebServer&)>& execute) {
#ifdef XP_FFI_HOST_FAULT_INJECTION
  const char* failAfter = std::getenv("WEB_FAIL_AFTER");
  xp_test_fail_allocation_after(failAfter ? std::strtoul(failAfter, nullptr, 10) : SIZE_MAX);
#endif
  host::trace.clear();
  host::files.clear();
  host::failOperation.clear();
  host::failAfter = -1;
  host::writeLimit = SIZE_MAX;
  host::cancelled = false;
  host::connected = true;
  host::now = 1000;
  SETTINGS = {};
  WiFi = {};
  WIFI_STORE.credentials = {{"Original", "secret"}};
  OPDS_STORE.servers = {{"Original", "https://example.test", "user", "secret", {{{"Auth", "token"}, {"", ""}}}}};
  PluginLocations::entries = {{"test", "/plugins/test"}};
  HalMemory::psram = false;
  freeink::fetchResult = {};
  freeink::fetchRewind = freeink::fetchCrossOrigin = false;
  freeink::fetchWait = 0;
  wsUploadFile.close();
  wsUploadFileName = "";
  wsUploadPath = "";
  wsLastCompleteName = "";
  wsUploadSize = wsUploadReceived = wsLastProgressSent = wsLastCompleteSize = wsLastCompleteAt = 0;
  wsUploadInProgress = false;
  wsUploadClientNum = 255;
  host::add("/", "", true);
  host::add("/books", "", true);
  host::add("/books/a.epub", "original");
  host::add("/dest", "", true);
  {
    CrossPointWebServer app;
    app.server = std::make_unique<CrossPointHttpServer>(80);
    app.wsServer = std::make_unique<WebSocketsServer>();
    app.running = true;
    app.setUploadCancelCheck([] { return host::cancelled; });
    execute(app);
  }
#ifdef XP_FFI_HOST_FAULT_INJECTION
  if (xp_test_live_allocations() != 0 || wsUploadInProgress) {
    std::cerr << "retained Rust allocation or upload owner after " << name << '\n';
    std::abort();
  }
#endif
  JsonDocument output;
  output["case"] = name;
  auto settings = output["settings"].to<JsonObject>();
  settings["toggle"] = SETTINGS.toggle;
  settings["enum"] = SETTINGS.enumeration;
  settings["value"] = SETTINGS.value;
  settings["text"] = SETTINGS.text;
  settings["password"] = SETTINGS.password;
  auto networks = output["networks"].to<JsonArray>();
  for (const auto& c : WIFI_STORE.credentials) {
    auto n = networks.add<JsonObject>();
    n["ssid"] = c.ssid;
    n["password"] = c.password;
  }
  auto servers = output["opds"].to<JsonArray>();
  for (const auto& s : OPDS_STORE.servers) {
    auto n = servers.add<JsonObject>();
    n["name"] = s.name;
    n["url"] = s.url;
    n["username"] = s.username;
    n["password"] = s.password;
    auto h = n["headers"].to<JsonArray>();
    for (auto& v : s.customHeaders) {
      auto item = h.add<JsonObject>();
      item["name"] = v.name;
      item["value"] = v.value;
    }
  }
  auto events = output["events"].to<JsonArray>();
  for (const auto& event : host::trace) events.add(event);
  auto files = output["files"].to<JsonObject>();
  for (const auto& [path, node] : host::files) {
    auto file = files[path].to<JsonObject>();
    file["directory"] = node->directory;
    file["hex"] = host::hex(reinterpret_cast<const uint8_t*>(node->bytes.data()), node->bytes.size());
  }
  serializeJson(output, std::cout);
  std::cout << '\n';
}

static void upload(CrossPointWebServer& app, HTTPUploadStatus status, const std::string& bytes = "") {
  app.server->part.status = status;
  app.server->part.buf = reinterpret_cast<const uint8_t*>(bytes.data());
  app.server->part.currentSize = bytes.size();
  app.handleUpload(app.upload);
}

static void text(CrossPointWebServer& app, uint8_t client, const std::string& bytes) {
  std::vector<uint8_t> payload(bytes.begin(), bytes.end());
  payload.push_back(0);
  app.onWebSocketEvent(client, WStype_TEXT, payload.data(), bytes.size());
}

int main() {
  for (const auto& path : {"", "/", "books//a.epub", "/books/../books/a.epub", "/books/missing", "/books",
                           "/.crosspoint/wifi.json", "/CROSSP~1/WIFI.JSON", "/XTCache", "/books/.hidden"}) {
    record(("download:" + std::string(path)).c_str(), [&](auto& app) {
      app.server->args["path"] = path;
      app.handleDownload();
    });
  }
  record("download-missing", [](auto& app) { app.handleDownload(); });
  for (const auto* failure : {"open", "alloc", "read", "client-write"}) {
    record(("download-fail:" + std::string(failure)).c_str(), [&](auto& app) {
      host::failOperation = failure;
      host::failAfter = 0;
      app.server->args["path"] = "/books/a.epub";
      app.handleDownload();
    });
  }
  for (const auto& name :
       {"", ".", "..", "a/b", "a\\b", ".hidden", "XTCache", "System Volume Information", "normal", "book..v2"}) {
    record(("mkdir:" + std::string(name)).c_str(), [&](auto& app) {
      app.server->args["name"] = name;
      app.server->args["path"] = "/books";
      app.handleCreateFolder();
    });
    record(("rename:" + std::string(name)).c_str(), [&](auto& app) {
      app.server->args["name"] = name;
      app.server->args["path"] = "/books/a.epub";
      app.handleRename();
    });
  }
  for (const auto& dest : {"", "/", "/dest", "/books", "/missing", "/.crosspoint", "/books/a.epub"}) {
    record(("move:" + std::string(dest)).c_str(), [&](auto& app) {
      app.server->args["path"] = "/books/a.epub";
      app.server->args["dest"] = dest;
      app.handleMove();
    });
  }
  for (const auto& paths : {"", "[]", "null", "{}", "[\"/books/a.epub\"]", "[\"/books\",\"/dest\",\"/missing\",\"/\"]",
                            "[\"/.crosspoint/wifi.json\",\"/CROSSP~1/WIFI.JSON\",\"/XTCache\"]", "[1,true,null,{}]"}) {
    record(("delete:" + std::string(paths)).c_str(), [&](auto& app) {
      app.server->args["paths"] = paths;
      app.handleDelete();
    });
  }
  for (const auto* failure : {"", "alloc", "create", "write"}) {
    for (const size_t size : {0, 1, 4095, 4096, 4097}) {
      record(("upload:" + std::string(failure) + ":" + std::to_string(size)).c_str(), [&](auto& app) {
        host::failOperation = failure;
        host::failAfter = 0;
        app.server->part.filename = "new.epub";
        app.server->args["path"] = "/books";
        upload(app, UPLOAD_FILE_START);
        upload(app, UPLOAD_FILE_WRITE, std::string(size, 'x'));
        upload(app, UPLOAD_FILE_END);
        app.handleUploadPost(app.upload);
      });
    }
  }
  for (const auto& filename : {"a.epub", "../a.epub", "bad/name", ".", "..", "", "new.epub"}) {
    record(("upload-name:" + std::string(filename)).c_str(), [&](auto& app) {
      app.server->part.filename = filename;
      app.server->args["path"] = "/books";
      upload(app, UPLOAD_FILE_START);
      upload(app, UPLOAD_FILE_END);
      app.handleUploadPost(app.upload);
    });
  }
  record("upload-cancel", [](auto& app) {
    app.server->part.filename = "cancel.epub";
    upload(app, UPLOAD_FILE_START);
    upload(app, UPLOAD_FILE_WRITE, "partial");
    host::cancelled = true;
    upload(app, UPLOAD_FILE_WRITE, "cancelled");
    upload(app, UPLOAD_FILE_ABORTED);
    app.handleUploadPost(app.upload);
  });
  for (const auto& control :
       {"START:new.epub:4:/books", "START:new.epub:0:/books", "START:new.epub:+4:/books", "START:new.epub:-1:/books",
        "START:new.epub:x:/books", "START:new.epub::/books", "START:new.epub:+:/books",
        "START:new.epub:99999999999999:/books", "START:bad", "START:../bad:4:/books", "START:a.epub:4:/books"}) {
    record(control, [&](auto& app) {
      text(app, 1, control);
      uint8_t data[] = {0, 1, 2, 3};
      app.onWebSocketEvent(1, WStype_BIN, data, sizeof(data));
      app.onWebSocketEvent(1, WStype_DISCONNECTED, nullptr, 0);
    });
  }
  for (int kind = 0; kind < 5; ++kind) {
    record(("ws-ownership:" + std::to_string(kind)).c_str(), [&](auto& app) {
      text(app, 1, "START:new.epub:4:/books");
      if (kind == 0) text(app, 1, "START:retry.epub:4:/books");
      if (kind == 1) text(app, 2, "START:retry.epub:4:/books");
      if (kind == 2) app.onWebSocketEvent(2, WStype_DISCONNECTED, nullptr, 0);
      if (kind == 3) {
        host::failOperation = "write";
        host::failAfter = 0;
      }
      uint8_t data[] = {0, 1, 2, 3, 4};
      app.onWebSocketEvent(kind == 2 ? 2 : 1, WStype_BIN, data, kind == 4 ? 5 : 4);
      app.onWebSocketEvent(1, WStype_DISCONNECTED, nullptr, 0);
    });
  }
  for (const auto& paths : {"/books/new.txt", "/.crosspoint/wifi.json", "/CROSSP~1/WIFI.JSON", "/../new", "relative"}) {
    for (int kind = 0; kind < 6; ++kind) {
      record(("plugin-fs:" + std::string(paths) + ":" + std::to_string(kind)).c_str(), [&](auto& app) {
        app.server->args["plugin"] = "test";
        app.server->args["path"] = paths;
        if (kind == 1) SETTINGS.pluginsEnabled = false;
        if (kind == 2) {
          host::failOperation = "write";
          host::failAfter = 0;
        }
        if (kind == 3) {
          host::failOperation = "replace";
          host::failAfter = 0;
        }
        app.server->part.status = UPLOAD_FILE_START;
        app.handlePluginFsUpload();
        const std::string data(kind == 4 ? 262145 : kind == 5 ? 0 : 4, 'x');
        app.server->part.status = UPLOAD_FILE_WRITE;
        app.server->part.buf = reinterpret_cast<const uint8_t*>(data.data());
        app.server->part.currentSize = data.size();
        app.handlePluginFsUpload();
        app.server->part.status = UPLOAD_FILE_END;
        app.handlePluginFsUpload();
        app.handlePluginFs();
      });
    }
  }
  for (const auto& body : {"", "bad", "{}", "{\"plugin\":\"p\",\"action\":\"run\"}",
                           "{\"plugin\":\"..\",\"action\":\"run\"}", "{\"plugin\":\"p\",\"action\":\"bad/act\"}"}) {
    record(("job-submit:" + std::string(body)).c_str(), [&](auto& app) {
      app.server->args["plain"] = body;
      app.handlePluginJobSubmit();
      app.server->args["plugin"] = "p";
      app.handlePluginJobClaim();
      app.server->args["id"] = "1";
      app.handlePluginJobStatus();
    });
  }
  record("job-leases", [](auto& app) {
    app.server->args["plain"] = "{\"plugin\":\"p\",\"action\":\"run\",\"args\":{\"x\":1}}";
    for (int i = 0; i < 7; ++i) app.handlePluginJobSubmit();
    app.server->args["plugin"] = "p";
    app.handlePluginJobClaim();
    host::now += 600001;
    app.handlePluginJobClaim();
    app.server->args["plain"] = "{\"id\":1,\"claim\":1,\"ok\":true}";
    app.handlePluginJobComplete();
    app.server->args["plain"] = "{\"id\":1,\"claim\":2,\"ok\":true,\"result\":{\"x\":3}}";
    app.handlePluginJobComplete();
    app.server->args["id"] = "1";
    app.handlePluginJobStatus();
    app.handlePluginJobComplete();
    app.server->args["plain"] = "{\"plugin\":\"p\",\"action\":\"recycled\"}";
    app.handlePluginJobSubmit();
    app.handlePluginJobStatus();
  });
  for (const size_t n : {180, 189, 190, 191, 192, 200}) {
    record(("job-size:" + std::to_string(n)).c_str(), [&](auto& app) {
      app.server->args["plain"] = "{\"plugin\":\"p\",\"action\":\"run\",\"args\":\"" + std::string(n, 'x') + "\"}";
      app.handlePluginJobSubmit();
      app.server->args["plugin"] = "p";
      app.handlePluginJobClaim();
      app.server->args["plain"] = "{\"id\":1,\"claim\":1,\"ok\":false,\"result\":\"" + std::string(n, 'x') + "\"}";
      app.handlePluginJobComplete();
      app.server->args["id"] = "1";
      app.handlePluginJobStatus();
    });
  }
  for (bool ap : {false, true}) {
    for (HTTPMethod method : {HTTP_GET, HTTP_POST, HTTP_OPTIONS}) {
      for (const auto* uri : {"/unknown", "/api/unknown"}) {
        record(("fallback:" + std::to_string(ap) + ":" + std::to_string(method) + ":" + uri).c_str(), [&](auto& app) {
          app.apMode = ap;
          app.server->requestMethod = method;
          app.server->requestUri = uri;
          app.handleNotFound();
        });
      }
    }
  }
  for (bool hidden : {false, true}) {
    record(("list:" + std::to_string(hidden)).c_str(), [&](auto& app) {
      SETTINGS.showHiddenFiles = hidden;
      host::add("/.hidden");
      host::add("/XTCache", "", true);
      host::add("/quote\".epub");
      host::add("/" + std::string(490, 'x'));
      app.handleFileListData();
    });
  }
  using Handler = void (CrossPointWebServer::*)();
  const std::pair<const char*, Handler> jsonHandlers[] = {
      {"settings", &CrossPointWebServer::handlePostSettings},
      {"opds", &CrossPointWebServer::handlePostOpdsServer},
      {"opds-delete", &CrossPointWebServer::handleDeleteOpdsServer},
      {"wifi", &CrossPointWebServer::handlePostWifiNetwork},
      {"wifi-delete", &CrossPointWebServer::handleDeleteWifiNetwork},
      {"relay", &CrossPointWebServer::handleRelay},
      {"crypto", &CrossPointWebServer::handleCrypto},
      {"fetch", &CrossPointWebServer::handleFetch},
      {"book-key", &CrossPointWebServer::handleBookKey},
      {"font-delete", &CrossPointWebServer::handleFontDelete}};
  for (auto [name, handler] : jsonHandlers) {
    for (const auto* body :
         {static_cast<const char*>(nullptr), "", "bad", "{}", "[]", "null", "{\"index\":-1}", "{\"index\":9}"}) {
      record((std::string(name) + ":invalid:" + (body ? body : "missing")).c_str(), [&](auto& app) {
        if (body) app.server->args["plain"] = body;
        (app.*handler)();
      });
    }
    record((std::string(name) + ":disabled").c_str(), [&](auto& app) {
      SETTINGS.pluginsEnabled = false;
      app.server->args["plain"] = "{}";
      (app.*handler)();
    });
  }
  record("settings-get", [](auto& app) { app.handleGetSettings(); });
  for (const auto* body : {"{\"toggle\":2,\"enum\":1,\"value\":7,\"text\":\"new\",\"password\":\"\"}",
                           "{\"toggle\":false,\"enum\":-1,\"value\":100,\"hidden\":\"ignored\"}",
                           "{\"toggle\":null,\"enum\":\"1\",\"value\":true,\"text\":123,\"unknown\":3}",
                           "{\"text\":\"long string truncated at capacity\",\"password\":\"updated\"}"}) {
    record((std::string("settings:") + body).c_str(), [&](auto& app) {
      app.server->args["plain"] = body;
      app.handlePostSettings();
      app.handleGetSettings();
    });
  }
  for (const auto* body :
       {"{\"name\":\"Added\",\"url\":\"https://example.test\"}", "{\"index\":0,\"name\":\"Renamed\"}",
        "{\"index\":0,\"password\":\"\",\"headers\":[{\"name\":\"New\"}]}",
        "{\"index\":0,\"headers\":[{\"name\":\"\"}]}",
        "{\"index\":0,\"headers\":[{\"name\":\"Auth\",\"value\":\"updated\"}]}"}) {
    record((std::string("opds:") + body).c_str(), [&](auto& app) {
      app.server->args["plain"] = body;
      app.handlePostOpdsServer();
      app.handleGetOpdsServers();
    });
  }
  record("opds-delete-valid", [](auto& app) {
    app.server->args["plain"] = "{\"index\":0}";
    app.handleDeleteOpdsServer();
    app.handleGetOpdsServers();
  });
  record("opds-limit", [](auto& app) {
    OPDS_STORE.servers.resize(8);
    app.server->args["plain"] = "{}";
    app.handlePostOpdsServer();
  });
  for (const auto* body :
       {"{\"ssid\":\"Added\",\"password\":\"new\"}", "{\"index\":0,\"ssid\":\"Original\"}",
        "{\"index\":0,\"ssid\":\"Renamed\"}", "{\"index\":0,\"ssid\":\"Original\",\"password\":\"\"}"}) {
    record((std::string("wifi:") + body).c_str(), [&](auto& app) {
      app.server->args["plain"] = body;
      app.handlePostWifiNetwork();
      app.handleGetWifiNetworks();
    });
  }
  record("wifi-delete-valid", [](auto& app) {
    app.server->args["plain"] = "{\"index\":0}";
    app.handleDeleteWifiNetwork();
    app.handleGetWifiNetworks();
  });
  record("wifi-limit", [](auto& app) {
    WIFI_STORE.credentials.resize(8);
    app.server->args["plain"] = "{\"ssid\":\"new\"}";
    app.handlePostWifiNetwork();
  });
  for (const auto* operation : {"random", "sha1", "sha256", "aesenc", "aesdec", "rsadec", "keygen", "pubencrypt",
                                "sign", "pkcs12", "unknown"}) {
    for (const auto* failure : {"", "crypto", "alloc"}) {
      record((std::string("crypto:") + operation + ":" + failure).c_str(), [&](auto& app) {
        host::failOperation = failure;
        host::failAfter = 0;
        JsonDocument req;
        req["op"] = operation;
        uint8_t bytes[32] = {};
        req["key"] = base64::encode(bytes, 16);
        req["iv"] = base64::encode(bytes, 16);
        req["data"] = base64::encode(bytes, 32);
        req["private"] = "AA==";
        req["cert"] = "AA==";
        req["hash"] = base64::encode(bytes, 20);
        String body;
        serializeJson(req, body);
        app.server->args["plain"] = body;
        app.handleCrypto();
      });
    }
  }
  for (int n : {-1, 0, 4096, 4097}) {
    record(("crypto-random-limit:" + std::to_string(n)).c_str(), [&](auto& app) {
      app.server->args["plain"] = "{\"op\":\"random\",\"len\":" + std::to_string(n) + "}";
      app.handleCrypto();
    });
  }
  for (int kind = 0; kind < 10; ++kind) {
    record(("fetch:" + std::to_string(kind)).c_str(), [&](auto& app) {
      const bool resume = kind == 2 || kind == 3 || kind == 4;
      app.server->args["plain"] =
          "{\"url\":\"https://example.test/book\",\"dest\":\"/books/"
          "new.epub\",\"headers\":{\"Authorization\":\"test\"},\"offset\":" +
          std::to_string(resume ? 4 : 0) + ",\"maxBytes\":" + std::to_string(kind == 5 ? 2 : 0) + "}";
      if (kind == 3 || kind == 4) host::add("/books/new.epub.part", "part");
      freeink::fetchRewind = kind == 4;
      freeink::fetchCrossOrigin = kind == 1;
      freeink::fetchWait = kind == 1 ? 5001 : 0;
      if (kind == 6) {
        host::failOperation = "write";
        host::failAfter = 0;
      }
      if (kind == 7) {
        host::failOperation = "replace";
        host::failAfter = 0;
      }
      if (kind == 8) freeink::fetchResult.status = 404;
      if (kind == 9) freeink::fetchResult = {-1, 0, false};
      app.handleFetch();
    });
  }
  for (const auto* failure : {"", "relay"}) {
    record((std::string("relay:") + failure).c_str(), [&](auto& app) {
      host::failOperation = failure;
      host::failAfter = 0;
      app.server->args["plain"] =
          "{\"plugin\":\"test\",\"url\":\"https://"
          "example.test\",\"headers\":{\"X-Test\":\"yes\"},\"body\":\"payload\"}";
      app.handleRelay();
    });
  }
  for (int n : {0, 16, 20, 32}) {
    record(("book-key:" + std::to_string(n)).c_str(), [&](auto& app) {
      uint8_t bytes[32] = {};
      app.server->args["plain"] = "{\"path\":\"/books/new.epub\",\"key\":\"" + base64::encode(bytes, n) + "\"}";
      app.handleBookKey();
    });
  }
  record("plugin-list", [](auto& app) {
    host::add("/plugins/test/manifest.json", "{\"title\":\"Title\",\"mount\":\"files\"}");
    app.handlePluginList();
  });
  for (const auto* name : {"test", "..", "missing"}) {
    record((std::string("plugin-file:") + name).c_str(), [&](auto& app) {
      host::add("/plugins/test/plugin.js", "hello");
      app.server->args["name"] = name;
      app.server->args["file"] = "plugin.js";
      app.handlePluginFile();
    });
  }
  record("font-list", [](auto& app) {
    host::add("/fonts/Test/font.cpfont", "fontdata");
    app.handleFontList();
  });
  for (const auto* filename : {"font.cpfont", "../bad.cpfont", "font.ttf"}) {
    for (int kind = 0; kind < 7; ++kind) {
      record((std::string("font-upload:") + filename + ":" + std::to_string(kind)).c_str(), [&](auto& app) {
        HalMemory::psram = kind == 1;
        app.server->args["family"] = "Test";
        app.server->part.filename = filename;
        if (kind == 2) {
          host::failOperation = "alloc";
          host::failAfter = 0;
        }
        if (kind == 3) {
          host::failOperation = "write";
          host::failAfter = 0;
        }
        app.server->part.status = UPLOAD_FILE_START;
        app.handleFontUploadData();
        std::string bytes(kind == 4 ? "invalid!" : "CPFONT\0\0", 8);
        if (HalMemory::psram) bytes = "OTTOdata";
        app.server->part.buf = reinterpret_cast<const uint8_t*>(bytes.data());
        app.server->part.currentSize = kind == 5 ? 4 : 8;
        app.server->part.status = UPLOAD_FILE_WRITE;
        app.handleFontUploadData();
        app.server->part.status = kind == 6 ? UPLOAD_FILE_ABORTED : UPLOAD_FILE_END;
        app.handleFontUploadData();
        app.handleFontUpload();
      });
    }
  }
  record("font-delete-valid", [](auto& app) {
    app.server->args["plain"] = "{\"family\":\"Test\"}";
    app.handleFontDelete();
  });
  for (int mode : {0, WIFI_MODE_STA, WIFI_MODE_AP}) {
    record(("lifecycle:" + std::to_string(mode)).c_str(), [&](auto& app) {
      app.running = false;
      app.server.reset();
      WiFi.mode = mode;
      app.begin();
      app.begin();
      app.handleClient();
      app.stop();
      app.stop();
    });
  }
  record("status", [](auto& app) {
    app.server->args["plugin"] = "Test";
    app.handleStatus();
  });
  record("pages", [](auto& app) {
    app.handleRoot();
    app.handleJszip();
    app.handleFileList();
    app.handleSettingsPage();
    app.handleFontsPage();
    app.handlePluginRunnerPage();
  });
  record("page-cache", [](auto& app) {
    app.server->headers["If-None-Match"] = HomePageHtmlETag;
    app.handleRoot();
  });
  record("registered-routes", [](auto& app) {
    app.running = false;
    app.begin();
    for (const auto& [route, handler] : app.server->routes) {
      host::trace.push_back("request:" + route);
      app.server->args.clear();
      handler();
    }
  });
  return 0;
}
