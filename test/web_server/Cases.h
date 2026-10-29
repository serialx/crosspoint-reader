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
  host::cryptoTrace = false;
  host::chunkStaticBodies = false;
  host::beforeResume = {};
  pluginhttp::beforeRequest = {};
  freeink::content::beforePkcs12 = {};
  host::cancelled = false;
  host::connected = true;
  host::now = 1000;
  host::discoveryPacket.clear();
  host::serialBytes = "TEST-SERIAL";
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
  pluginhttp::responseStatus = 201;
  pluginhttp::responseSize = 8;
  pluginhttp::timeout = false;
  freeink::fetchResult = {};
  freeink::fetchRewind = freeink::fetchCrossOrigin = false;
  freeink::fetchWait = 0;
  freeink::fetchSteps.clear();
  freeink::fetchScripted = false;
  freeink::beforeFetch = {};
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
    if (std::getenv("WEB_FETCH_ALLOCATION_CHECK") && std::string_view(name).starts_with("fetch") &&
        xp_test_allocation_attempts() != 0) {
      std::cerr << "fetch route allocated Rust memory: " << name << '\n';
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
  if (std::getenv("WEB_TRACE_LIMIT_CHECK")) host::trace.emplace_back(65000, 'x');
  auto events = output["events"].to<JsonArray>();
  for (const auto& event : host::trace) {
    if (event.size() >= 65000) {
      std::cerr << "transcript string limit: " << name << " event " << events.size() << '\n';
      std::abort();
    }
    events.add(event);
  }
  auto files = output["files"].to<JsonObject>();
  for (const auto& [path, node] : host::files) {
    auto file = files[path].to<JsonObject>();
    file["directory"] = node->directory;
    if (node->bytes.size() >= 32000) {
      auto chunks = file["hex_chunks"].to<JsonArray>();
      for (size_t offset = 0; offset < node->bytes.size(); offset += 2048) {
        chunks.add(host::hex(reinterpret_cast<const uint8_t*>(node->bytes.data() + offset),
                             std::min(size_t(2048), node->bytes.size() - offset)));
      }
    } else {
      file["hex"] = host::hex(reinterpret_cast<const uint8_t*>(node->bytes.data()), node->bytes.size());
    }
  }
  if (std::getenv("WEB_TRACE_AUDIT")) {
    JsonDocument audit;
    audit["case"] = name;
    audit["source_events"] = host::trace.size();
    audit["recorded_events"] = events.size();
    audit["overflowed"] = output.overflowed();
    audit["files_present"] = output["files"].is<JsonObject>();
    size_t largest = 0;
    auto oversized = audit["oversized_events"].to<JsonArray>();
    for (size_t i = 0; i < host::trace.size(); ++i) {
      largest = std::max(largest, host::trace[i].size());
      if (host::trace[i].size() >= 65000) {
        auto event = oversized.add<JsonObject>();
        event["index"] = i;
        event["bytes"] = host::trace[i].size();
        event["prefix"] = host::trace[i].substr(0, 48);
      }
    }
    audit["largest_event_bytes"] = largest;
    serializeJson(audit, std::cerr);
    std::cerr << '\n';
  }
  if (output.overflowed() || events.size() != host::trace.size() || files.size() != host::files.size()) {
    std::cerr << "transcript overflow: " << name << '\n';
    std::abort();
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

  if (const char* check = std::getenv("WEB_STRING_LIFETIME_CHECK")) {
    const std::string_view mode(check);
    const auto verify = [](size_t expected, const char* stage) {
      const size_t live = String::liveStorage();
      std::cerr << "String storage " << stage << ": " << live << " B, baseline " << expected << " B\n";
      if (live != expected) std::abort();
    };
    if (mode == "tls") {
      record("string-lifetime-tls", [&](auto& app) {
        app.server->args["plain"] =
            "{\"plugin\":\"p\",\"url\":\"http://example.test\",\"body\":\"" + std::string(32768, 'x') + "\"}";
        const size_t baseline = String::liveStorage();
        pluginhttp::beforeRequest = [&] { verify(baseline, "TLS entry"); };
        app.handleRelay();
        pluginhttp::beforeRequest = {};
      });
    } else if (mode == "pkcs12") {
      record("string-lifetime-pkcs12", [&](auto& app) {
        app.server->args["plain"] =
            "{\"op\":\"pkcs12\",\"data\":\"" + std::string(65532, 'A') + "\",\"password\":\"synthetic\"}";
        const size_t baseline = String::liveStorage();
        bool entered = false;
        freeink::content::beforePkcs12 = [&] {
          entered = true;
          verify(baseline, "PKCS12 entry");
        };
        app.handleCrypto();
        freeink::content::beforePkcs12 = {};
        if (!entered) std::abort();
      });
      record("string-lifetime-65536-field", [&](auto& app) {
        app.server->args["plain"] = "{\"op\":\"pkcs12\",\"data\":\"" + std::string(65536, 'A') + "\"}";
        app.handleCrypto();
      });
    } else if (mode == "fetch") {
      for (int kind = 0; kind < 3; ++kind) {
        record("string-lifetime-fetch", [&](auto& app) {
          app.server->args["plain"] =
              "{\"url\":\"http://example.test\",\"dest\":\"/books/new.epub\",\"headers\":{\"X-Test\":\"" +
              std::string(32768, 'x') + "\"},\"maxBytes\":" + std::to_string(kind == 2 ? 2 : 0) + "}";
          freeink::fetchResult.complete = kind != 1;
          const size_t baseline = String::liveStorage();
          bool entered = false;
          freeink::beforeFetch = [&] {
            entered = true;
            if (WebJsonAllocator::live) std::abort();
            verify(baseline, "fetch entry");
          };
          host::beforeResume = [&] { verify(baseline, "fetch restart"); };
          app.handleFetch();
          freeink::beforeFetch = {};
          host::beforeResume = {};
          if (!entered) std::abort();
        });
      }
    } else if (mode == "resume") {
      for (int status : {-1, 200}) {
        record("string-lifetime-resume", [&](auto& app) {
          app.server->args["plain"] = R"({"plugin":"p","url":"http://example.test"})";
          pluginhttp::responseSize = 32768;
          pluginhttp::responseStatus = status;
          const size_t baseline = String::liveStorage();
          host::beforeResume = [&] { verify(baseline, "service restart"); };
          app.handleRelay();
          host::beforeResume = {};
        });
      }
    }
    return 0;
  }
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

  for (int status : {-1, 0, 199, 200, 204, 206, 299, 300, 302, 400, 404, 416, 500}) {
    for (int kind = 0; kind < 12; ++kind) {
      record(("fetch-script:" + std::to_string(status) + ":" + std::to_string(kind)).c_str(), [&](auto& app) {
        app.server->args["plain"] =
            "{\"url\":\"https://example.test/book\",\"dest\":\"/books/"
            "new.epub\",\"headers\":{\"Authorization\":\"token\"},\"offset\":" +
            std::to_string(kind == 7 || kind == 8 ? 4 : 0) + ",\"maxBytes\":" + std::to_string(kind == 9 ? 2 : 0) + "}";
        host::add("/books/new.epub", "existing");
        host::add("/books/new.epub.part", "part");
        freeink::fetchScripted = true;
        freeink::fetchResult = {status, kind == 10 ? size_t(0) : size_t(8), kind != 1 && kind != 2};
        freeink::fetchSteps = {{2, 4999}, {2, 1}, {4, 5000}};
        if (kind == 1) freeink::fetchSteps = {{2}, {0, 60001}};
        if (kind == 2) freeink::fetchSteps = {{2}, {0, 60001, true}};
        if (kind == 3) freeink::fetchSteps = {{0, 5000, true}, {8}};
        if (kind == 4) freeink::fetchSteps = {{2, 5000}, {6, 5000, true}};
        if (kind == 5) freeink::fetchSteps = {{0, 0, false, true}, {8, 60001}};
        if (kind == 6) freeink::fetchSteps = {{2}, {6, 5000, false, true}};
        if (kind == 7) freeink::fetchSteps = {{0, 0, false, false, true}, {4}};
        if (kind == 8) freeink::fetchSteps = {{4, 5000}};
        if (kind == 10) freeink::fetchSteps = {};
        if (kind == 11) freeink::fetchSteps = {{2}, {0, 0, false, false, true}, {8}};
        app.handleFetch();
      });
    }
  }
  for (const char* failure : {"create", "open", "write", "replace", "remove", "mkdir"}) {
    for (int after : {0, 1, 2}) {
      record((std::string("fetch-storage:") + failure + ":" + std::to_string(after)).c_str(), [&](auto& app) {
        host::failOperation = failure;
        host::failAfter = after;
        host::add("/books/new.epub", "existing");
        host::add("/books/new.epub.part", "part");
        app.server->args["plain"] = "{\"url\":\"http://example.test\",\"dest\":\"/books/new.epub\",\"offset\":" +
                                    std::to_string(std::string_view(failure) == "open" ? 4 : 0) + "}";
        freeink::fetchScripted = true;
        freeink::fetchSteps = {{2}, {2}, {0, 5000, false, false, true}, {4}};
        app.handleFetch();
      });
    }
  }
  for (int limit : {0, 1, 3, 4}) {
    record(("fetch-short-write:" + std::to_string(limit)).c_str(), [&](auto& app) {
      host::writeLimit = limit;
      app.server->args["plain"] = "{\"url\":\"http://example.test\",\"dest\":\"/books/new.epub\"}";
      app.handleFetch();
    });
  }
  for (const char* field : {"url", "dest", "offset", "maxBytes", "headers"}) {
    for (const char* value :
         {"null", "false", "true", "[]", "{}", "0", "-1", "2147483647", "2147483648", "4294967295", "4194305", "1.5",
          "\" 4\"", "\"\"", "\"http://example.test\\u0000suffix\"", "\"/books/new.epub\\u0000suffix\""}) {
      record((std::string("fetch-json:") + field + ":" + value).c_str(), [&](auto& app) {
        app.server->args["plain"] = "{\"url\":\"http://example.test\",\"dest\":\"/books/new.epub\",\"" +
                                    std::string(field) + "\":" + value + "}";
        app.handleFetch();
      });
    }
  }
  for (int after : {0, 1, 2, 3, 4, 5, 8, 16}) {
    record(("fetch-json-oom:" + std::to_string(after)).c_str(), [&](auto& app) {
      WebJsonAllocator::failAfter = after;
      app.server->args["plain"] = "{\"url\":\"http://example.test\",\"dest\":\"/books/new.epub\"}";
      app.handleFetch();
      WebJsonAllocator::failAfter = -1;
    });
  }
  for (int kind = 0; kind < 4; ++kind) {
    record(("fetch-recovery:" + std::to_string(kind)).c_str(), [&](auto& app) {
      freeink::fetchScripted = true;
      freeink::fetchSteps = {{2}};
      freeink::fetchResult = {200, 4, false};
      app.server->args["plain"] = "{\"url\":\"http://example.test\",\"dest\":\"/books/new.epub\",\"maxBytes\":2}";
      app.handleFetch();
      freeink::fetchResult = {206, 4, true};
      if (kind == 1) {
        freeink::fetchRewind = true;
        freeink::fetchResult.status = 200;
      }
      if (kind == 2) {
        host::failOperation = "write";
        host::failAfter = 0;
      }
      app.server->args["plain"] = "{\"url\":\"http://example.test\",\"dest\":\"/books/new.epub\",\"offset\":" +
                                  std::to_string(kind == 3 ? 3 : 2) + "}";
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

  for (const size_t size : {188, 189, 190, 191, 192, 512}) {
    for (bool ok : {false, true}) {
      record(("jobs-result-bound:" + std::to_string(size) + ":" + std::to_string(ok)).c_str(), [&](auto& app) {
        app.server->args["plain"] =
            "{\"plugin\":\"p\",\"action\":\"12345678901234567890123\",\"args\":\"" + std::string(189, 'x') + "\"}";
        app.handlePluginJobSubmit();
        app.server->args["plugin"] = "p";
        app.handlePluginJobClaim();
        app.server->args["plain"] = std::string("{\"id\":1,\"claim\":1,\"ok\":") + (ok ? "true" : "false") +
                                    ",\"result\":\"" + std::string(size, 'y') + "\"}";
        app.handlePluginJobComplete();
        app.server->args["id"] = "1";
        app.handlePluginJobStatus();
        app.handlePluginJobComplete();
      });
    }
  }

  record("jobs-status-vertical-tab", [](auto& app) {
    app.server->args["plain"] = R"({"plugin":"p","action":"run"})";
    app.handlePluginJobSubmit();
    app.server->args["id"] = "\v1";
    app.handlePluginJobStatus();
  });

  for (const char* route : {"relay", "crypto"}) {
    for (const char* body : {"", "null", "[]", "42", "true", "{}", "{", R"({"op":null})"}) {
      record((std::string("rc-json:") + route + ":" + body).c_str(), [&](auto& app) {
        app.server->uploadAvailable = false;
        app.server->args["plain"] = body;
        if (std::string(route) == "relay")
          app.handleRelay();
        else
          app.handleCrypto();
      });
    }
  }
  for (const char* field : {"plugin", "url", "method", "body", "headers"}) {
    for (const char* value : {"null", "42", "true", "[]", "{}", R"("")", R"("a/b")", R"("a..b")", R"("a\\b")",
                              R"("a\u0000/b")", R"("\u0000x")", R"({"X-A":null,"X-B":[1],"X-C":42})"}) {
      record((std::string("relay-field:") + field + ":" + value).c_str(), [&](auto& app) {
        app.server->args["plain"] =
            std::string(R"({"plugin":"p","url":"https://example.test",)") + "\"" + field + "\":" + value + "}";
        app.handleRelay();
      });
    }
  }
  for (int status : {-3, -1, 0, 200, 301, 400, 401, 404, 500, 599}) {
    record(("relay-status:" + std::to_string(status)).c_str(), [&](auto& app) {
      pluginhttp::responseStatus = status;
      app.server->args["plain"] = R"({"plugin":"p","url":"https://example.test"})";
      app.handleRelay();
    });
  }
  for (size_t size : {0, 32767, 32768, 32769}) {
    record(("relay-body-limit:" + std::to_string(size)).c_str(), [&](auto& app) {
      pluginhttp::responseSize = size;
      app.server->args["plain"] = R"({"plugin":"p","url":"https://example.test"})";
      app.handleRelay();
    });
  }
  record("relay-timeout", [&](auto& app) {
    pluginhttp::timeout = true;
    app.server->args["plain"] = R"({"plugin":"p","url":"https://example.test"})";
    app.handleRelay();
  });
  for (const char* value :
       {"null", "true", "1.5", "-1", "0", "1", "4095", "4096", "4097", "2147483648", "4294967295", R"("22")", "[]"}) {
    record((std::string("crypto-random-coercion:") + value).c_str(), [&](auto& app) {
      app.server->args["plain"] = std::string(R"({"op":"random","len":)") + value + "}";
      app.handleCrypto();
    });
  }
  for (const char* op : {"sha1", "sha256", "aesenc", "aesdec", "rsadec", "pubencrypt", "sign", "pkcs12", "random"}) {
    for (const char* value :
         {"null", "42", "true", "[]", R"("")", R"("!")", R"("AA==\u0000junk")", R"(" AA==")", R"("AAAA")"}) {
      record((std::string("crypto-input:") + op + ":" + value).c_str(), [&](auto& app) {
        app.server->args["plain"] =
            std::string("{\"op\":\"") + op + "\",\"data\":" + value + ",\"hash\":" + value + "}";
        host::cryptoTrace = true;
        app.handleCrypto();
      });
    }
  }
  for (const char* op :
       {"sha1", "sha256", "aesenc", "aesdec", "rsadec", "pubencrypt", "sign", "pkcs12", "keygen", "random"}) {
    for (int budget = 0; budget < 7; ++budget) {
      record((std::string("crypto-alloc:") + op + ":" + std::to_string(budget)).c_str(), [&](auto& app) {
        app.server->args["plain"] =
            std::string("{\"op\":\"") + op +
            R"(","key":"AAAAAAAAAAAAAAAAAAAAAA==","iv":"AAAAAAAAAAAAAAAAAAAAAA==","data":"AA==","private":"AA==","cert":"AA==","hash":"AAAAAAAAAAAAAAAAAAAAAAAAAAA="})";
        host::failOperation = "alloc";
        host::failAfter = budget;
        host::cryptoTrace = true;
        app.handleCrypto();
      });
    }
    for (const char* failure : {"encode-empty", "encode-fail", "crypto", "aes-encrypt"}) {
      record((std::string("crypto-failure:") + op + ":" + failure).c_str(), [&](auto& app) {
        app.server->args["plain"] =
            std::string("{\"op\":\"") + op +
            R"(","key":"AAAAAAAAAAAAAAAAAAAAAA==","iv":"AAAAAAAAAAAAAAAAAAAAAA==","data":"AAAAAAAAAAAAAAAAAAAAAA==","private":"AA==","cert":"AA==","hash":"AAAAAAAAAAAAAAAAAAAAAAAAAAA="})";
        host::failOperation = failure;
        host::failAfter = 0;
        host::cryptoTrace = true;
        app.handleCrypto();
      });
    }
  }
  for (size_t size : {0, 15, 16, 17, 20, 31, 32, 65535, 65536, 65537}) {
    for (const char* field : {"data", "key", "iv", "hash"}) {
      record((std::string("crypto-field-bound:") + field + ":" + std::to_string(size)).c_str(), [&](auto& app) {
        app.server->args["plain"] =
            std::string(R"({"op":"aesdec","key":"AAAAAAAAAAAAAAAAAAAAAA==","iv":"AAAAAAAAAAAAAAAAAAAAAA==",)") + "\"" +
            field + "\":\"" + std::string(size, 'A') + "\"}";
        host::cryptoTrace = true;
        app.handleCrypto();
      });
    }
  }
  for (const char* route : {"relay", "crypto"}) {
    for (int budget = 0; budget < 12; ++budget) {
      record((std::string("rc-json-alloc:") + route + ":" + std::to_string(budget)).c_str(), [&](auto& app) {
        app.server->args["plain"] = R"({"plugin":"p","url":"https://example.test","op":"keygen"})";
        WebJsonAllocator::failAfter = budget;
        if (std::string(route) == "relay")
          app.handleRelay();
        else
          app.handleCrypto();
        WebJsonAllocator::failAfter = -1;
      });
    }
  }

  record("relay-release-before-tls", [](auto& app) {
    app.server->_currentArgs = new String[1];
    app.server->_currentArgCount = 1;
    app.server->_postArgs = new String[1];
    app.server->_postArgsLen = 1;
    app.server->args["plain"] =
        R"({"plugin":"p","url":"https://example.test","headers":{"Authorization":"synthetic"},"body":"payload"})";
    pluginhttp::beforeRequest = [&] {
      if (app.server->_currentArgs || app.server->_postArgs || WebJsonAllocator::live) std::abort();
    };
    app.handleRelay();
    pluginhttp::beforeRequest = {};
  });
  record("crypto-pkcs12-release", [](auto& app) {
    app.server->args["plain"] = R"({"op":"pkcs12","data":"AA==","password":"synthetic"})";
    freeink::content::beforePkcs12 = [] {
      if (WebJsonAllocator::live) std::abort();
    };
    app.handleCrypto();
    freeink::content::beforePkcs12 = {};
  });

  for (const char* op : {R"("sha1\u0000suffix")", R"("aesdec\u0000suffix")", R"("\u0000sha1")", "null", "42", "[]"}) {
    record((std::string("crypto-op-coercion:") + op).c_str(), [&](auto& app) {
      app.server->args["plain"] = std::string("{\"op\":") + op + R"(,"data":"AA=="})";
      host::cryptoTrace = true;
      app.handleCrypto();
    });
  }

  for (int status : {-1, 200}) {
    record(("relay-response-release:" + std::to_string(status)).c_str(), [&](auto& app) {
      app.server->args["plain"] = R"({"plugin":"p","url":"https://example.test"})";
      pluginhttp::responseStatus = status;
      host::beforeResume = [] {
        if (WebJsonAllocator::live) std::abort();
      };
      app.handleRelay();
      host::beforeResume = {};
    });
  }
  for (int mode : {0, 1, 2, 3}) {
    for (int connected : {0, WL_CONNECTED}) {
      for (int stations : {-1, 0, 2}) {
        record(
            ("life-network:" + std::to_string(mode) + ":" + std::to_string(connected) + ":" + std::to_string(stations))
                .c_str(),
            [&](auto& app) {
              app.stop();
              WiFi.mode = mode;
              WiFi.statusValue = connected;
              WiFi.stations = stations;
              app.begin();
              app.begin();
              app.handleClient();
              app.stop();
              app.handleClient();
              app.stop();
              app.begin();
            });
      }
    }
  }
  for (int kind = 0; kind < 7; ++kind) {
    record(("life-transfer:" + std::to_string(kind)).c_str(), [&](auto& app) {
      if (kind == 0) app.stop();
      if (kind == 1) {
        app.server->args["path"] = "/books";
        app.server->part.filename = "transfer.epub";
        upload(app, UPLOAD_FILE_START);
      }
      if (kind == 2 || kind == 3) text(app, 4, "START:transfer.epub:4:/books");
      if (kind == 4) {
        host::failOperation = "udp";
        host::failAfter = 0;
      }
      app.udpActive = kind != 5;
      app.suspendTransferServices();
      app.suspendTransferServices();
      if (kind == 3) app.stop();
      app.resumeTransferServices();
      app.resumeTransferServices();
      if (kind == 1) upload(app, UPLOAD_FILE_ABORTED);
      if (kind == 2) {
        app.onWebSocketEvent(4, WStype_DISCONNECTED, nullptr, 0);
        app.suspendTransferServices();
        app.resumeTransferServices();
      }
    });
  }
  for (const std::string packet : {std::string(), std::string("hello"), std::string("hello\0suffix", 12),
                                   std::string("Hello"), std::string("hello!"), std::string(32, 'x')}) {
    for (const std::string hostname : {std::string(), std::string("oracle"), std::string(100, 'h')}) {
      record(("life-discovery:" + host::hex(reinterpret_cast<const uint8_t*>(packet.data()), packet.size()) + ":" +
              std::to_string(hostname.size()))
                 .c_str(),
             [&](auto& app) {
               app.udpActive = true;
               WiFi.hostname = hostname;
               for (uint32_t now : {10000u, 10001u, 20001u, 20002u}) {
                 host::now = now;
                 host::discoveryPacket = packet;
                 app.handleClient();
               }
             });
    }
  }
  for (const std::string serial : {std::string(), std::string("OK"), std::string(32, 'S'), std::string("bad\nserial"),
                                   std::string(1, char(255))}) {
    for (bool ap : {false, true}) {
      record(("life-status-serial:" + host::hex(reinterpret_cast<const uint8_t*>(serial.data()), serial.size()) + ":" +
              std::to_string(ap))
                 .c_str(),
             [&](auto& app) {
               app.apMode = ap;
               host::serialBytes = serial;
               app.handleStatus();
             });
    }
  }
  for (const std::string plugin :
       {std::string(), std::string("TEST"), std::string(64, 'a'), std::string(65, 'a'), std::string("a\0B", 3)}) {
    for (const char* failure : {"none", "secret", "sha256", "efuse"}) {
      record(("life-status-plugin:" + host::hex(reinterpret_cast<const uint8_t*>(plugin.data()), plugin.size()) + ":" +
              failure)
                 .c_str(),
             [&](auto& app) {
               host::cryptoTrace = true;
               app.server->args["plugin"] = String(plugin.data(), plugin.size());
               host::failOperation = failure;
               host::failAfter = 0;
               host::now = UINT32_MAX;
               app.handleStatus();
             });
    }
  }
  for (int after = 0; after < 4; ++after) {
    record(("life-status-oom:" + std::to_string(after)).c_str(), [&](auto& app) {
      WebJsonAllocator::failAfter = after;
      app.handleStatus();
      WebJsonAllocator::failAfter = -1;
      app.handleStatus();
    });
  }
  for (uint32_t elapsed : {4999u, 5000u, 5001u, 59999u, 60000u, 60001u}) {
    record(("fetch-rollover:" + std::to_string(elapsed)).c_str(), [&](auto& app) {
      app.server->args["plain"] = R"({"url":"https://test/file","dest":"/plugins/test/book.epub"})";
      host::now = UINT32_MAX - 2000;
      freeink::fetchScripted = true;
      freeink::fetchSteps = {{0, elapsed}, {4}};
      app.handleFetch();
    });
  }

  for (size_t length : {254u, 255u, 256u, 1023u, 1024u, 1025u}) {
    record(("life-upload-name:" + std::to_string(length)).c_str(), [&](auto& app) {
      app.server->args["path"] = "/books";
      app.server->part.filename = String(std::string(length, 'n').c_str());
      upload(app, UPLOAD_FILE_START);
      upload(app, UPLOAD_FILE_ABORTED);
    });
    record(("life-upload-path:" + std::to_string(length)).c_str(), [&](auto& app) {
      const std::string path = "/" + std::string(length, 'p');
      host::add(path, "", true);
      app.server->args["path"] = path.c_str();
      app.server->part.filename = "test.epub";
      upload(app, UPLOAD_FILE_START);
      upload(app, UPLOAD_FILE_ABORTED);
    });
  }
  for (const char* path : {"/", "/files", "/js/jszip.min.js", "/assets/web.css", "/assets/icons.svg",
                           "/assets/crosspoint-icon.svg", "/settings", "/fonts", "/plugins-run"}) {
    record((std::string("life-static:") + path).c_str(), [&](auto& app) {
      app.running = false;
      app.begin();
      auto handler = app.server->routes.at(std::string("0:") + path);
      handler();
      const auto it = std::find_if(host::trace.rbegin(), host::trace.rend(),
                                   [](const auto& line) { return line.starts_with("header:ETag:"); });
      if (it == host::trace.rend()) std::abort();
      const auto etag = it->substr(12);
      app.server->headers["If-None-Match"] = etag.c_str();
      handler();
      app.server->headers["If-None-Match"] = "mismatch";
      handler();
    });
  }
  for (const char* failure : {"http-alloc", "ws-alloc", "udp"}) {
    record((std::string("life-oom:") + failure).c_str(), [&](auto& app) {
      app.stop();
      host::failOperation = failure;
      host::failAfter = 0;
      app.begin();
      if (app.isRunning()) {
        app.suspendTransferServices();
        app.resumeTransferServices();
        app.resumeTransferServices();
      } else {
        app.begin();
      }
    });
  }
  for (const char* path : {"/", "/files", "/js/jszip.min.js", "/assets/web.css", "/assets/icons.svg",
                           "/assets/crosspoint-icon.svg", "/settings", "/fonts", "/plugins-run"}) {
    record((std::string("life-static-cstring:") + path).c_str(), [&](auto& app) {
      host::chunkStaticBodies = true;
      app.running = false;
      app.begin();
      auto handler = app.server->routes.at(std::string("0:") + path);
      handler();
      const auto it = std::find_if(host::trace.rbegin(), host::trace.rend(),
                                   [](const auto& line) { return line.starts_with("header:ETag:"); });
      if (it == host::trace.rend()) std::abort();
      const auto etag = it->substr(12);
      for (const auto& value : {etag, etag + std::string("\0suffix", 7), std::string("\0", 1) + etag,
                                etag + std::string("\0different", 10), std::string("mismatch")}) {
        app.server->headers["If-None-Match"] = String(value.data(), value.size());
        handler();
      }
    });
  }
  return 0;
}
