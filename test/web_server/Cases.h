#pragma once

#ifdef XP_FFI_HOST_FAULT_INJECTION
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();
extern "C" size_t xp_test_allocation_attempts();
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
  String::failAllocationSize = SIZE_MAX;
  String::failedAllocations = 0;
  WebJsonAllocator::failAfter = -1;
  SETTINGS = {};
  extendedSettings = false;
  WiFi = {};
  WIFI_STORE.credentials = {{"Original", "secret"}};
  OPDS_STORE.servers = {{"Original", "https://example.test", "user", "secret", {{{"Auth", "token"}, {"", ""}}}}};
  PluginLocations::entries = {{"test", "/plugins/test"}};
  HalMemory::psram = false;
  sdFontSystem.fonts.families = {{}};
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
#ifdef XP_FFI_HOST_FAULT_INJECTION
    if (std::getenv("WEB_JOB_ALLOCATION_CHECK") && std::string_view(name).starts_with("job") &&
        xp_test_allocation_attempts() != 0) {
      std::cerr << "job route allocated Rust memory: " << name << '\n';
      std::abort();
    }
    if (wsUploadInProgress && !wsUploadFile) {
      std::cerr << "active upload has no file before teardown: " << name << '\n';
      std::abort();
    }
    if (wsLastCompleteAt != 0) {
      const std::string suffix = "/" + std::string(wsLastCompleteName.c_str());
      const bool found = std::any_of(host::files.begin(), host::files.end(), [&](const auto& file) {
        return file.first.ends_with(suffix) && file.second->bytes.size() == wsLastCompleteSize;
      });
      if (!found) {
        std::cerr << "completion has no matching file before teardown: " << name << '\n';
        std::abort();
      }
    }
#endif
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
#ifdef XP_FFI_HOST_FAULT_INJECTION
  if (std::getenv("WEB_KEY_LIFETIME_CHECK")) {
    record("book-key-body-lifetime", [](auto& app) {
      app.server->args["plain"] =
          std::string(16384, ' ') + R"({"path":"/books/new.epub","key":"AAAAAAAAAAAAAAAAAAAAAA=="})";
      app.handleBookKey();
      if (std::none_of(host::trace.begin(), host::trace.end(),
                       [](const auto& event) { return event.starts_with("bookkey:"); }))
        std::abort();
    });
    return 0;
  }
  if (std::getenv("WEB_LOCALIZATION_CHECK")) {
    memoryTranslation = "메모리 오류";
    for (int kind = 0; kind < 3; ++kind)
      record("localized-allocation-failure", [&](auto& app) {
        xp_test_fail_allocation_after(0);
        if (kind == 0) {
          app.server->args["path"] = "/books/a.epub";
          app.handleDownload();
        }
        if (kind == 1) {
          app.server->part.filename = "new.epub";
          upload(app, UPLOAD_FILE_START);
          app.handleUploadPost(app.upload);
        }
        if (kind == 2) text(app, 1, "START:new.epub:4:/books");
        const std::string expected =
            kind == 2 ? "ws:1:ERROR:메모리 오류"
                      : (kind == 0 ? "response:503:text/plain:메모리 오류" : "response:400:text/plain:메모리 오류");
        if (std::find(host::trace.begin(), host::trace.end(), expected) == host::trace.end()) {
          std::cerr << "missing translated error: " << expected << '\n';
          for (const auto& e : host::trace) std::cerr << e << '\n';
          std::abort();
        }
        xp_test_fail_allocation_after(SIZE_MAX);
      });
    return 0;
  }
  if (std::getenv("WEB_SYSTEM_NAME_CHECK")) {
    for (bool showHidden : {false, true})
      record("system-name-listing", [&](auto& app) {
        SETTINGS.showHiddenFiles = showHidden;
        const std::pair<const char*, bool> names[] = {
            {"XTCache", true},  {"System Volume Information", true},   {"XTCacheArchive", false},
            {"xtcache", false}, {"System Volume Information ", false}, {".hidden", false},
            {"ordinary", false}};
        for (const auto& [name, system] : names) host::add("/" + std::string(name), "", true);
        app.handleFileListData();
        std::string body;
        for (const auto& event : host::trace)
          if (event.starts_with("chunk:")) body += event.substr(6);
        JsonDocument listing;
        if (deserializeJson(listing, body)) std::abort();
        for (const auto& [name, system] : names) {
          bool found = false;
          for (JsonObject item : listing.as<JsonArray>())
            if (std::string(item["name"].as<const char*>()) == name) found = true;
          if (found != !(system || (!showHidden && name[0] == '.'))) {
            std::cerr << "incorrect system-name listing: " << name << '\n';
            std::abort();
          }
        }
      });
    return 0;
  }
  if (std::getenv("WEB_COMPLETION_CHECK")) {
    for (bool previous : {false, true})
      record("completion-allocation-failure", [&](auto& app) {
        if (previous) text(app, 1, "START:previous.epub:0:/books");
        const auto before = app.getWsUploadStatus();
        text(app, 1, "START:failed.epub:4:/books");
        xp_test_fail_allocation_after(0);
        uint8_t bytes[4] = {1, 2, 3, 4};
        app.onWebSocketEvent(1, WStype_BIN, bytes, sizeof(bytes));
        const auto after = app.getWsUploadStatus();
        if (after.inProgress || wsUploadFile || host::files.contains("/books/failed.epub") ||
            after.lastCompleteName != before.lastCompleteName || after.lastCompleteSize != before.lastCompleteSize ||
            after.lastCompleteAt != before.lastCompleteAt) {
          std::cerr << "completion allocation failure published success or retained partial upload\n";
          std::abort();
        }
        xp_test_fail_allocation_after(SIZE_MAX);
      });
    return 0;
  }
#endif
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
  for (const auto* point : {"READY", "PROGRESS:", "DONE"}) {
    record((std::string("ws-synchronous-disconnect:") + point).c_str(), [&](auto& app) {
      bool disconnected = false;
      app.wsServer->onSend = [&](uint8_t client, const String& message) {
        if (!disconnected && message.startsWith(point)) {
          disconnected = true;
          app.onWebSocketEvent(client, WStype_DISCONNECTED, nullptr, 0);
        }
      };
      const auto checkDisconnected = [&] {
        if (disconnected && (wsUploadInProgress || wsUploadFile)) {
          std::cerr << "disconnect did not release upload before outer event returned\n";
          std::abort();
        }
      };
      text(app, 1, "START:new.epub:131072:/books");
      checkDisconnected();
      std::vector<uint8_t> data(65536, 'x');
      app.onWebSocketEvent(1, WStype_BIN, data.data(), data.size());
      checkDisconnected();
      app.onWebSocketEvent(1, WStype_BIN, data.data(), data.size());
      checkDisconnected();
      text(app, 2, "START:retry.epub:0:/books");
    });
  }
  record("ws-disconnect-stop-restart", [](auto& app) {
    app.wsServer->onSend = [&](uint8_t client, const String& message) {
      if (message.startsWith("PROGRESS:")) app.onWebSocketEvent(client, WStype_DISCONNECTED, nullptr, 0);
    };
    text(app, 1, "START:interrupted.epub:131072:/books");
    std::vector<uint8_t> bytes(65536, 'x');
    app.onWebSocketEvent(1, WStype_BIN, bytes.data(), bytes.size());
    if (wsUploadInProgress || wsUploadFile) std::abort();
    app.stop();
    app.begin();
    text(app, 2, "START:after-restart.epub:0:/books");
  });
  for (bool wifi : {false, true})
    record(wifi ? "wifi-escaped-json" : "opds-escaped-json", [&](auto& app) {
      app.server->args["plain"] =
          R"({"ssid":"A\u0000B","name":"A\u0000B","url":"x\n\t\"\\/","username":"U\u0000V","password":"P\u0000Q","headers":[{"name":"H\u0000I","value":"V\u0000W"}]})";
      if (wifi) {
        app.handlePostWifiNetwork();
        app.handleGetWifiNetworks();
      } else {
        app.handlePostOpdsServer();
        app.handleGetOpdsServers();
      }
    });
  record("settings-extra-descriptors", [](auto& app) {
    extendedSettings = true;
    app.handleGetSettings();
    app.server->args["plain"] =
        "{\"getter-enum\":1,\"getter-string\":\"changed\",\"getter-secret\":\"\",\"no-pointer\":1,\"negative-range\":-"
        "3,\"no-string\":\"ignored\",\"action\":1}";
    app.handlePostSettings();
    app.handleGetSettings();
  });
  // Coercion and secret-preservation cases recorded from the C++ handlers.
  for (const auto* value : {"null", "true", "false", "0", "-1", "2147483648", "1.5", "\"\"", "\"1\"", "[]", "{}"}) {
    for (const auto* family : {"settings", "opds", "wifi"}) {
      record((std::string("config-types:") + family + ":" + value).c_str(), [&](auto& app) {
        std::string body;
        if (std::string(family) == "settings") {
          body = std::string("{\"toggle\":") + value + ",\"enum\":" + value + ",\"choices\":" + value +
                 ",\"value\":" + value + ",\"text\":" + value + ",\"password\":" + value + "}";
          app.server->args["plain"] = body;
          app.handlePostSettings();
          app.handleGetSettings();
        } else {
          body = std::string("{\"index\":0,\"ssid\":\"Original\",\"password\":") + value +
                 ",\"headers\":[{\"name\":\"Auth\",\"value\":" + value + "}]}";
          app.server->args["plain"] = body;
          if (std::string(family) == "opds") {
            app.handlePostOpdsServer();
            app.handleGetOpdsServers();
          } else {
            app.handlePostWifiNetwork();
            app.handleGetWifiNetworks();
          }
        }
      });
    }
    for (const auto* family : {"opds", "wifi", "opds-delete", "wifi-delete"}) {
      record((std::string("config-index:") + family + ":" + value).c_str(), [&](auto& app) {
        app.server->args["plain"] = std::string("{\"ssid\":\"Original\",\"index\":") + value + "}";
        if (std::string(family) == "opds")
          app.handlePostOpdsServer();
        else if (std::string(family) == "wifi")
          app.handlePostWifiNetwork();
        else if (std::string(family) == "opds-delete")
          app.handleDeleteOpdsServer();
        else
          app.handleDeleteWifiNetwork();
      });
    }
  }
  for (const auto* headers : {"null", "{}", "[]", "[null,1]", "[{\"name\":\"\",\"value\":\"token\"}]",
                              "[{\"name\":\"A\"},{\"name\":\"B\"},{\"name\":\"C\"}]"}) {
    for (bool update : {false, true})
      record((std::string("opds-headers:") + headers + (update ? ":update" : ":add")).c_str(), [&](auto& app) {
        app.server->args["plain"] = std::string(update ? "{\"index\":0,\"headers\":" : "{\"headers\":") + headers + "}";
        app.handlePostOpdsServer();
        app.handleGetOpdsServers();
      });
  }
  for (const auto* failure : {"opds-add", "wifi-add", "wifi-remove"}) {
    record((std::string("config-failure:") + failure).c_str(), [&](auto& app) {
      host::failOperation = failure;
      host::failAfter = 0;
      app.server->args["plain"] = "{\"index\":0,\"ssid\":\"Renamed\"}";
      if (std::string(failure) == "opds-add") {
        app.server->args["plain"] = "{}";
        app.handlePostOpdsServer();
      } else {
        app.handlePostWifiNetwork();
        app.handleDeleteWifiNetwork();
      }
    });
  }
  for (bool wifi : {false, true})
    record(wifi ? "wifi-skip-first" : "opds-skip-first", [&](auto& app) {
      if (wifi) {
        WIFI_STORE.credentials = {{std::string(400, 'x'), "secret"}, {"short", ""}};
        app.handleGetWifiNetworks();
      } else {
        OPDS_STORE.servers[0].name = std::string(600, 'x');
        OPDS_STORE.servers.push_back({});
        app.handleGetOpdsServers();
      }
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

  for (int mode = 0; mode < 7; ++mode) {
    record(("fonts-list-extra:" + std::to_string(mode)).c_str(), [&](auto& app) {
      auto& families = sdFontSystem.fonts.families;
      if (mode == 0) families.clear();
      if (mode == 1) families[0].files.clear();
      if (mode == 2) {
        families[0].name = "Quote\"\\\t\n";
        families[0].files = {{"a\"b.cpfont"}, {"second.cpfont"}};
        families[0].sizes = {0, 255};
      }
      if (mode == 3) families[0].name = std::string(150, 'x');
      if (mode == 4) {
        families.resize(20);
        for (size_t i = 0; i < families.size(); ++i) families[i].name = "Family" + std::to_string(i);
      }
      if (mode == 5) {
        HalMemory::psram = true;
        families[0].sizes.clear();
        families[0].files = {{"regular.ttf"}, {"bold.otf"}};
      }
      if (mode == 6) {
        host::add("/fonts/Test/font.cpfont", "data");
        host::failOperation = "open";
        host::failAfter = 0;
      }
      app.handleFontList();
    });
  }
  for (const char* body : {"", "{", "null", "[]", "{}", "{\"family\":null}", "{\"family\":7}", "{\"family\":true}",
                           "{\"family\":[]}", "{\"family\":{}}", "{\"family\":\"\"}", "{\"family\":\"../bad\"}",
                           "{\"family\":\"A\\u0000B\"}", "{\"family\":\"A\\\"\\\\B\"}"}) {
    for (bool fail : {false, true}) {
      record((std::string("fonts-delete-extra:") + body + ":" + std::to_string(fail)).c_str(), [&](auto& app) {
        app.server->args["plain"] = body;
        if (fail) {
          host::failOperation = "font-delete";
          host::failAfter = 0;
        }
        app.handleFontDelete();
      });
    }
  }
  for (const char* family : {"", "Test", "../bad", "bad/name", "bad\\name"}) {
    for (const char* filename : {"with space.cpfont", "font.otf", "font.ttf", "../bad.cpfont", "font.cpfont"}) {
      for (bool psram : {false, true}) {
        record((std::string("fonts-start:") + family + ":" + filename + ":" + std::to_string(psram)).c_str(),
               [&](auto& app) {
                 HalMemory::psram = psram;
                 app.server->args["family"] = family;
                 app.server->part.filename = filename;
                 app.server->part.status = UPLOAD_FILE_START;
                 app.handleFontUploadData();
                 app.server->part.status = UPLOAD_FILE_ABORTED;
                 app.handleFontUploadData();
                 app.handleFontUpload();
               });
      }
    }
  }
  for (size_t size :
       {size_t(0), size_t(1), size_t(7), size_t(8), size_t(4095), size_t(4096), size_t(4097), size_t(8200)}) {
    for (int mode = 0; mode < 10; ++mode) {
      record(("fonts-data:" + std::to_string(size) + ":" + std::to_string(mode)).c_str(), [&](auto& app) {
        app.server->args["family"] = "Test";
        app.server->part.filename = "font.cpfont";
        if (mode == 1) {
          host::failOperation = "font-dir";
          host::failAfter = 0;
        }
        if (mode == 2) {
          host::failOperation = "create";
          host::failAfter = 0;
        }
        app.server->part.status = UPLOAD_FILE_START;
        app.handleFontUploadData();
        std::string data(size, 'x');
        if (size >= 8 && mode != 3) data.replace(0, 8, "CPFONT\0\0", 8);
        app.server->part.buf = reinterpret_cast<const uint8_t*>(data.data());
        app.server->part.currentSize = size;
        if (mode == 4) host::cancelled = true;
        if (mode == 5) {
          host::failOperation = "write";
          host::failAfter = 0;
        }
        if (mode == 6) host::writeLimit = 3;
        if (mode == 7) {
          host::failOperation = "write";
          host::failAfter = 1;
        }
        app.server->part.status = UPLOAD_FILE_WRITE;
        app.handleFontUploadData();
        if (mode == 8) app.handleFontUploadData();
        app.server->part.status = mode == 9 ? UPLOAD_FILE_ABORTED : UPLOAD_FILE_END;
        app.handleFontUploadData();
        app.handleFontUpload();
      });
    }
  }
  for (const std::string magic :
       {std::string("\0\1\0\0data", 8), std::string("OTTOdata"), std::string("truedata"), std::string("bad!data")}) {
    record(("fonts-vector:" + std::to_string(static_cast<unsigned char>(magic[0]))).c_str(), [&](auto& app) {
      HalMemory::psram = true;
      app.server->args["family"] = "Test";
      app.server->part.filename = "font.ttf";
      app.server->part.status = UPLOAD_FILE_START;
      app.handleFontUploadData();
      app.server->part.buf = reinterpret_cast<const uint8_t*>(magic.data());
      app.server->part.currentSize = magic.size();
      app.server->part.status = UPLOAD_FILE_WRITE;
      app.handleFontUploadData();
      app.server->part.status = UPLOAD_FILE_END;
      app.handleFontUploadData();
      app.handleFontUpload();
    });
  }
  record("fonts-page-cache", [](auto& app) {
    app.server->headers["If-None-Match"] = FontsPageHtmlETag;
    app.handleFontsPage();
  });
  record("fonts-family-nul-state", [](auto& app) {
    app.server->args["family"] = String("Test\0ignored", 12);
    app.server->part.filename = "font.cpfont";
    app.server->part.status = UPLOAD_FILE_START;
    app.handleFontUploadData();
    host::trace.push_back("font-family:" + host::hex(reinterpret_cast<const uint8_t*>(app.fontUpload.familyName.data()),
                                                     app.fontUpload.familyName.size()));
    app.server->part.status = UPLOAD_FILE_ABORTED;
    app.handleFontUploadData();
  });
  for (const char* shape : {"empty", "plain", "json", "fields-only"}) {
    record((std::string("fonts-no-upload:") + shape).c_str(), [&](auto& app) {
      app.server->requestMethod = HTTP_POST;
      app.server->requestUri = "/api/fonts/upload";
      app.server->uploadAvailable = false;
      if (std::string(shape) == "plain") app.server->args["plain"] = "font bytes";
      if (std::string(shape) == "json") app.server->args["plain"] = "{}";
      if (std::string(shape) == "fields-only") app.server->args["family"] = "Test";
      app.handleFontUpload();
    });
  }
  record("fonts-list-response-allocation", [](auto& app) {
    app.handleFontList();
    const std::string prefix = "response:200:application/json:";
    const auto response = std::find_if(host::trace.begin(), host::trace.end(),
                                       [&](const auto& event) { return event.starts_with(prefix); });
    // Rust allocation sweeps may fail before the transport is reached.
    if (response == host::trace.end()) return;
    String::failAllocationSize = response->size() - prefix.size();
    app.handleFontList();
    host::trace.push_back("string-allocation-failures:" + std::to_string(String::failedAllocations));
    String::failAllocationSize = SIZE_MAX;
    app.handleFontList();
  });
  // Plugin slice: capture these cases from dc21f50f before replacing handlers.
  for (const char* manifest :
       {"", "bad", "{}", "null", "[]", "{\"title\":1,\"mount\":false}", "{\"title\":\"\",\"mount\":\"\"}",
        "{\"title\":\"a\\n\\\"b\\u0000tail\",\"mount\":\"files\"}"}) {
    record((std::string("plugins-manifest:") + manifest).c_str(), [&](auto& app) {
      PluginLocations::entries = {{"no-js", "/plugins/no-js", false, true},
                                  {"test", "/plugins/test", true, true},
                                  {"fallback", "/plugins/fallback", true, false}};
      if (*manifest) host::add("/plugins/test/manifest.json", manifest);
      app.handlePluginList();
    });
  }
  for (int kind = 0; kind < 5; ++kind) {
    record(("plugins-list-extra:" + std::to_string(kind)).c_str(), [&](auto& app) {
      if (kind == 0) PluginLocations::entries.clear();
      if (kind == 1) PluginLocations::entries = {{"q\"\n", "/plugins/q\"\n", true, false}};
      if (kind == 2) host::add("/plugins/test/manifest.json", std::string(65537, ' '));
      if (kind == 3) {
        host::failOperation = "open";
        host::failAfter = 0;
      }
      if (kind == 4) SETTINGS.pluginsEnabled = false;
      app.handlePluginList();
    });
  }
  for (const char* file : {"", "..", "a/b", "a\\b", "a..b", "plugin.js", "style.css", "page.html", "info.json",
                           "image.svg", "FILE.JS", ".hidden", "other.bin"}) {
    for (int kind = 0; kind < 3; ++kind) {
      record((std::string("plugins-file-extra:") + file + ":" + std::to_string(kind)).c_str(), [&](auto& app) {
        app.server->args["name"] = "test";
        app.server->args["file"] = file;
        if (kind != 0) host::add(std::string("/plugins/test/") + file, "bytes", kind == 2);
        app.handlePluginFile();
      });
    }
  }
  for (const char* shape : {"empty", "plain", "json", "fields-only"}) {
    for (bool enabled : {false, true}) {
      record((std::string("plugins-no-upload:") + shape + ":" + std::to_string(enabled)).c_str(), [&](auto& app) {
        SETTINGS.pluginsEnabled = enabled;
        app.server->uploadAvailable = false;
        if (std::string(shape) == "plain") app.server->args["plain"] = "bytes";
        if (std::string(shape) == "json") app.server->args["plain"] = "{}";
        if (std::string(shape) == "fields-only") app.server->args["plugin"] = "test";
        app.handlePluginFs();
      });
    }
  }
  for (const char* plugin : {"", "test", "..", "a/b", "a\\b", "a..b"}) {
    record((std::string("plugins-fs-name:") + plugin).c_str(), [&](auto& app) {
      app.server->args["plugin"] = plugin;
      app.server->args["path"] = "/plugins/test/auth.json";
      app.server->part.status = UPLOAD_FILE_START;
      app.handlePluginFsUpload();
      app.server->part.status = UPLOAD_FILE_ABORTED;
      app.handlePluginFsUpload();
      app.handlePluginFs();
    });
  }
  for (int kind = 0; kind < 10; ++kind) {
    record(("plugins-fs-extra:" + std::to_string(kind)).c_str(), [&](auto& app) {
      app.server->args["plugin"] = "test";
      app.server->args["path"] = kind == 0 ? "/file" : "/plugins/test/deep/book.epub";
      host::add(app.server->args["path"], "original");
      if (kind == 1) {
        host::failOperation = "create";
        host::failAfter = 0;
      }
      if (kind == 2) host::writeLimit = 2;
      app.server->part.status = UPLOAD_FILE_START;
      app.handlePluginFsUpload();
      const std::string data(kind == 3 ? 262144 : 4, 'x');
      app.server->part.status = UPLOAD_FILE_WRITE;
      app.server->part.buf = reinterpret_cast<const uint8_t*>(data.data());
      app.server->part.currentSize = data.size();
      app.handlePluginFsUpload();
      if (kind == 4) app.handlePluginFsUpload();
      if (kind == 5) SETTINGS.pluginsEnabled = false;
      if (kind == 6) {
        host::failOperation = "remove";
        host::failAfter = 0;
      }
      app.server->part.status = kind == 6 ? UPLOAD_FILE_ABORTED : UPLOAD_FILE_END;
      app.handlePluginFsUpload();
      if (kind == 7) String::failAllocationSize = 21;  // {"ok":true,"bytes":4}
      app.server->uploadAvailable = false;
      app.handlePluginFs();
      if (kind == 8) app.handlePluginFs();
      if (kind == 9) {
        app.server->uploadAvailable = true;
        app.server->part.status = UPLOAD_FILE_START;
        app.handlePluginFsUpload();
        app.server->part.status = UPLOAD_FILE_ABORTED;
        app.handlePluginFsUpload();
        app.handlePluginFs();
      }
    });
  }
  for (const char* expiry :
       {"-1", "0", "1", "2147483648", "9223372036854775807", "1.5", "\"12\"", "true", "null", "[]"}) {
    for (bool fail : {false, true}) {
      record((std::string("plugins-book-key:") + expiry + ":" + std::to_string(fail)).c_str(), [&](auto& app) {
        app.server->args["plain"] =
            std::string("{\"path\":\"/books/new.epub\",\"key\":\"AAAAAAAAAAAAAAAAAAAAAA==\",\"expires\":") + expiry +
            "}";
        if (fail) {
          host::failOperation = "bookkey";
          host::failAfter = 0;
        }
        app.handleBookKey();
      });
    }
  }
  for (const char* body : {"{\"path\":\"/.crosspoint/wifi.json\",\"key\":\"AAAAAAAAAAAAAAAAAAAAAA==\"}",
                           "{\"path\":\"/books/a\\u0000b\",\"key\":\"AAAAAAAAAAAAAAAAAAAAAA==\"}",
                           "{\"path\":12,\"key\":true}", "{\"path\":\"relative\",\"key\":\"bad\"}"}) {
    record((std::string("plugins-book-body:") + body).c_str(), [&](auto& app) {
      app.server->args["plain"] = body;
      app.handleBookKey();
    });
  }
  record("plugins-list-response-allocation", [](auto& app) {
    app.handlePluginList();
    const std::string prefix = "response:200:application/json:";
    const auto response = std::find_if(host::trace.begin(), host::trace.end(),
                                       [&](const auto& event) { return event.starts_with(prefix); });
    if (response == host::trace.end()) return;
    String::failAllocationSize = response->size() - prefix.size();
    app.handlePluginList();
    host::trace.push_back("string-allocation-failures:" + std::to_string(String::failedAllocations));
    String::failAllocationSize = SIZE_MAX;
    app.handlePluginList();
  });
  for (int kind = 0; kind < 6; ++kind) {
    record(("plugins-nul-component:" + std::to_string(kind)).c_str(), [&](auto& app) {
      host::add("/plugins/test/plugin.js", "asset");
      app.server->args["name"] = kind == 0 ? String("test\0/..", 8) : kind == 1 ? String("\0bad", 4) : String("test");
      app.server->args["file"] = kind == 2   ? String("plugin.js\0/..", 13)
                                 : kind == 3 ? String("\0.js", 4)
                                             : String("plugin.js");
      if (kind < 4) {
        app.handlePluginFile();
        return;
      }
      app.server->args["plugin"] = kind == 4 ? String("test\0/..", 8) : String("\0", 1);
      app.server->args["path"] = String("/books/new.epub\0/..", 18);
      app.server->part.status = UPLOAD_FILE_START;
      app.handlePluginFsUpload();
      app.server->part.status = UPLOAD_FILE_ABORTED;
      app.handlePluginFsUpload();
      app.handlePluginFs();
    });
  }

  // Queue policy: observe responses/logs, never inspect implementation storage.
  for (const uint32_t elapsed : {599999U, 600000U, 600001U}) {
    for (int mode = 0; mode < 4; ++mode) {
      record(("jobs-expiry:" + std::to_string(elapsed) + ":" + std::to_string(mode)).c_str(), [&](auto& app) {
        app.server->args["plain"] = R"({"plugin":"p","action":"run"})";
        app.handlePluginJobSubmit();
        app.server->args["plugin"] = "p";
        app.handlePluginJobClaim();
        host::now += elapsed;
        app.server->args["id"] = "1";
        app.handlePluginJobStatus();  // Status alone does not expire a lease.
        if (mode != 0) {
          app.server->args["plugin"] = mode == 2 ? "other" : "p";
          app.handlePluginJobClaim();
        }
        app.server->args["plain"] = mode == 3 ? R"({"id":1,"claim":2,"ok":false})" : R"({"id":1,"claim":1,"ok":true})";
        app.handlePluginJobComplete();
        app.handlePluginJobStatus();
        app.handlePluginJobComplete();
      });
    }
  }
  for (bool wrap : {false, true}) {
    record(("jobs-recycle:" + std::to_string(wrap)).c_str(), [&](auto& app) {
      host::now = wrap ? UINT32_MAX - 1000U : 1000U;
      app.server->args["plain"] = R"({"plugin":"p","action":"run"})";
      for (int i = 0; i < 7; ++i) app.handlePluginJobSubmit();
      app.server->args["plugin"] = "p";
      for (int i = 0; i < 7; ++i) app.handlePluginJobClaim();
      for (int i : {3, 1, 5}) {
        host::now = static_cast<uint32_t>(host::now + 501);
        app.server->args["plain"] =
            "{\"id\":" + std::to_string(i) + ",\"claim\":" + std::to_string(i) + ",\"ok\":false}";
        app.handlePluginJobComplete();
      }
      app.server->args["plain"] = R"({"plugin":"p","action":"new"})";
      for (int i = 0; i < 4; ++i) app.handlePluginJobSubmit();
      for (int i = 0; i <= 10; ++i) {
        app.server->args["id"] = std::to_string(i);
        app.handlePluginJobStatus();
      }
    });
    record(("jobs-wrap-lease:" + std::to_string(wrap)).c_str(), [&](auto& app) {
      host::now = wrap ? UINT32_MAX - 1000U : 1000U;
      app.server->args["plain"] = R"({"plugin":"p","action":"run"})";
      app.handlePluginJobSubmit();
      app.server->args["plugin"] = "p";
      app.handlePluginJobClaim();
      host::now = static_cast<uint32_t>(host::now + 600001);
      app.handlePluginJobClaim();
    });
  }
  for (const char* field : {"plugin", "action", "args"}) {
    for (const char* value : {"null", "false", "123", "1.5", "[]", "{}", "\"\"", "\"p\"", "\"a.b-c_d\"", "\"a b\"",
                              "\"a/b\"", "\"a\\\\b\"", "\"..\"", "\"é\"", "\"p\\u0000/..\"", "\"\\u0000p\"", "\"p\\n\"",
                              "\"12345678901234567890123\"", "\"123456789012345678901234\""}) {
      record((std::string("jobs-types:") + field + ":" + value).c_str(), [&](auto& app) {
        app.server->args["plain"] =
            std::string("{\"plugin\":\"p\",\"action\":\"run\",\"") + field + "\":" + value + "}";
        app.handlePluginJobSubmit();
        app.server->args["plugin"] = "p";
        app.handlePluginJobClaim();
        app.server->args["id"] = "1";
        app.handlePluginJobStatus();
      });
    }
  }
  for (const char* field : {"id", "claim", "ok", "result"}) {
    for (const char* value : {"null", "true", "false", "0", "1", "-1", "2147483648", "4294967296", "1.5", "\"1\"",
                              "\"p\\u0000x\"", "[]", "{}"}) {
      record((std::string("jobs-complete-types:") + field + ":" + value).c_str(), [&](auto& app) {
        app.server->args["plain"] = R"({"plugin":"p","action":"run"})";
        app.handlePluginJobSubmit();
        app.server->args["plugin"] = "p";
        app.handlePluginJobClaim();
        app.server->args["plain"] = std::string("{\"id\":1,\"claim\":1,\"ok\":true,\"") + field + "\":" + value + "}";
        app.handlePluginJobComplete();
        app.server->args["id"] = "1";
        app.handlePluginJobStatus();
      });
    }
  }
  for (int mode = 0; mode < 5; ++mode) {
    record(("jobs-lifecycle:" + std::to_string(mode)).c_str(), [&](auto& app) {
      app.server->args["plain"] = R"({"plugin":"p","action":"run"})";
      app.handlePluginJobSubmit();
      if (mode == 0) {
        app.stop();
        app.begin();
      }
      if (mode == 1) SETTINGS.pluginsEnabled = false;
      if (mode == 2) {
        SETTINGS.pluginsEnabled = false;
        app.handlePluginJobClaim();
        SETTINGS.pluginsEnabled = true;
      }
      if (mode == 3) {
        CrossPointWebServer rebooted;
        rebooted.server = std::make_unique<CrossPointHttpServer>(80);
        rebooted.server->args["id"] = "1";
        rebooted.handlePluginJobStatus();
      }
      app.server->args["plugin"] = mode == 4 ? String("p\0suffix", 8) : String("p");
      app.handlePluginJobClaim();
      app.handlePluginJobClaim();
      app.server->args["id"] = "1";
      app.handlePluginJobStatus();
      app.server->args["plain"] = R"({"id":1,"claim":1,"ok":true})";
      app.handlePluginJobComplete();
    });
  }
  for (const char* id : {"", "abc", " 1", "+1", "1tail", "-1", "4294967295", "0x1"}) {
    record((std::string("jobs-status-parse:") + id).c_str(), [&](auto& app) {
      app.server->args["plain"] = R"({"plugin":"p","action":"run"})";
      app.handlePluginJobSubmit();
      app.server->args["id"] = id;
      app.handlePluginJobStatus();
    });
  }
  for (int route = 0; route < 4; ++route) {
    for (bool disabled : {false, true}) {
      record(("jobs-no-file:" + std::to_string(route) + ":" + std::to_string(disabled)).c_str(), [&](auto& app) {
        SETTINGS.pluginsEnabled = !disabled;
        app.server->uploadAvailable = false;
        switch (route) {
          case 0:
            app.handlePluginJobSubmit();
            break;
          case 1:
            app.handlePluginJobClaim();
            break;
          case 2:
            app.handlePluginJobComplete();
            break;
          case 3:
            app.handlePluginJobStatus();
            break;
        }
      });
    }
  }

  for (int budget = 0; budget < 8; ++budget) {
    for (bool complete : {false, true}) {
      record(("jobs-json-oom:" + std::to_string(budget) + ":" + std::to_string(complete)).c_str(), [&](auto& app) {
        app.server->args["plain"] = R"({"plugin":"p","action":"run","args":{"v":[1,2,3]}})";
        if (complete) {
          app.handlePluginJobSubmit();
          app.server->args["plugin"] = "p";
          app.handlePluginJobClaim();
          app.server->args["plain"] = R"({"id":1,"claim":1,"ok":true,"result":{"v":[1,2,3]}})";
        }
        WebJsonAllocator::failAfter = budget;
        if (complete)
          app.handlePluginJobComplete();
        else
          app.handlePluginJobSubmit();
        WebJsonAllocator::failAfter = -1;
        app.server->args["id"] = "1";
        app.handlePluginJobStatus();
        if (complete)
          app.handlePluginJobComplete();
        else
          app.handlePluginJobSubmit();
      });
    }
  }
  return 0;
}
