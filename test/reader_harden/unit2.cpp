extern "C" int32_t xp_epub_test_harden_boundary(const XpAppResources*, const XpEpubLeaves*, const XpAppRuntime*,
                                                XpAppText, void (*)(), uint8_t, XpAppScreenshot*);
extern "C" uint32_t xp_epub_test_discover_parse(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText,
                                                void (*)());
extern "C" int32_t xp_epub_test_harden_progress(const XpAppResources*, const XpEpubLeaves*, XpAppText, void (*)(),
                                                uint8_t);
static std::string activeFault;
static size_t sizeErrorLogs;
static void injectUnit2Fault() {
  if (activeFault.ends_with("valid")) return;
  if (activeFault.ends_with("alloc")) {
    xp_test_fail_allocation_after(0);
    return;
  }
  if (activeFault.ends_with("open")) {
    HalFileTest::allowOpen = [](const char*, const char*) {
      ++faultHits;
      return false;
    };
  } else {
    HalFileTest::readLimit = [](const char*, size_t at, size_t count) {
      if (activeFault.starts_with("unit2-boundary") || activeFault == "unit2-size" || activeFault == "unit2-spine" ||
          activeFault == "unit2-progress" || activeFault == "unit2-mapping" || activeFault == "unit2-save" ||
          at == faultOffset) {
        ++faultHits;
        return size_t(0);
      }
      return count;
    };
  }
}
static int unit2(const std::string& mode, const std::string& root, size_t offset, XpAppResources& resources,
                 XpEpubLeaves& leaves) {
  activeFault = mode;
  faultOffset = offset;
  if (mode.starts_with("unit2-boundary")) {
    auto runtime = poisoned<XpAppRuntime>();
    runtime.model_lock = [] {};
    runtime.model_unlock = [] {};
    leaves.log_enabled = [](uint8_t severity) -> uint8_t { return severity == 1; };
    leaves.log = [](uint8_t severity, XpAppText, XpAppText message) {
      assert(severity == 1);
      const auto value = owned(message);
      assert(value.find("unreadable size window") != std::string::npos);
      ++sizeErrorLogs;
    };
    XpAppScreenshot info{};
    const bool known = mode.ends_with("known");
    const auto exit =
        xp_epub_test_harden_boundary(&resources, &leaves, &runtime, text(root), injectUnit2Fault, known, &info);
    check(faultHits >= 2, "size fault reaches screenshot and reader.exit queries");
    check(info.progress_percent == (known ? 33 : 0), "native screenshot ABI keeps known percent or initial zero");
    check(exit == (known ? 33 : 0), "reader.exit ABI keeps known percent or initial zero");
    checkBoundaryConsumers(info, exit);
    check(sizeErrorLogs == 2, "bookmark flag and commit each log one unreadable size window error");
  } else if (mode == "unit2-size" || mode == "unit2-spine") {
    BookMetadataCache cache(root);
    assert(cache.load());
    injectUnit2Fault();
    const auto value = mode == "unit2-size" ? xp_epub_test_cache_query(cache.testHandle(), 3, 64, nullptr, nullptr)
                                            : cache.getSpineIndexForSize(7100);
    check(value == -1 && faultHits != 0, "M3 failed window reports unknown");
    HalFileTest::readLimit = nullptr;
    check(cache.getCumulativeSize(64) == 6500, "M3 failed window can retry");
  } else if (mode.starts_with("unit2-progress") || mode.starts_with("unit2-mapping") ||
             mode.starts_with("unit2-save")) {
    const auto value =
        xp_epub_test_harden_progress(&resources, &leaves, text(root), injectUnit2Fault,
                                     mode.starts_with("unit2-save") ? 2 : mode.starts_with("unit2-mapping"));
    check(value == (mode == "unit2-save-valid"       ? 6400
                    : mode == "unit2-progress-valid" ? 6450
                    : mode == "unit2-mapping-valid"  ? 70
                                                     : -1),
          "M3 progress and mapping carry unknown");
    if (!mode.ends_with("valid")) check(faultHits != 0, "M3 fault reached consumer");
  } else {
    if (mode.ends_with("oom")) {
      resources.buffer_allocate = [](XpAppMemoryDomain, size_t) -> uint8_t* {
        ++faultHits;
        return nullptr;
      };
    }
    if (mode.starts_with("unit2-walk")) {
      injectUnit2Fault();
      const auto result =
          xp_epub_test_zip(&resources, &leaves, text(root + "/source.epub"), text(""), 3, receive, nullptr);
      check(result == mode.ends_with("valid"), "M4 incomplete directory walk fails");
    } else {
      const auto result =
          xp_epub_test_discover_parse(&resources, &leaves, text(root + "/source.epub"), text(root), injectUnit2Fault);
      xp_test_fail_allocation_after(SIZE_MAX);
      check(result == (mode.ends_with("valid") ? 0u : 258u), "M4 failed discovery blocks the later parse/save");
      check(Storage.exists((root + "/css_rules.cache").c_str()) == mode.ends_with("valid"),
            "M4 no incomplete CSS persisted");
    }
    if (!mode.ends_with("valid") && !mode.ends_with("alloc")) check(faultHits != 0, "M4 fault reached");
  }
  return failures ? 1 : 0;
}
