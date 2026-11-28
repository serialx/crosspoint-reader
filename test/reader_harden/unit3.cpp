extern "C" int32_t xp_epub_test_harden_rows(const XpAppResources*, const XpEpubLeaves*, XpAppText, void (*)(), uint8_t);
static std::string rowFault;
static void injectRowFault() {
  if (rowFault.ends_with("valid")) return;
  if (rowFault.ends_with("alloc")) {
    xp_test_fail_allocation_after(0);
    return;
  }
  if (rowFault.ends_with("seek")) {
    HalFileTest::allowSeek = [](const char*, size_t) {
      ++faultHits;
      return false;
    };
  } else {
    HalFileTest::readLimit = [](const char*, size_t, size_t count) {
      if (faultHits == 0 && count) {
        ++faultHits;
        return count - 1;
      }
      return count;
    };
  }
}
static int unit3(const std::string& mode, const std::string& root, const XpAppResources& resources,
                 const XpEpubLeaves& leaves) {
  rowFault = mode;
  const uint8_t kind = mode.find("spine-") != std::string::npos   ? 0
                       : mode.find("toc-") != std::string::npos   ? 1
                       : mode.find("index-") != std::string::npos ? 2
                       : mode.find("href-") != std::string::npos  ? 3
                                                                  : 4;
  const auto value = xp_epub_test_harden_rows(&resources, &leaves, text(root), injectRowFault, kind);
  xp_test_fail_allocation_after(SIZE_MAX);
  const bool valid = mode.ends_with("valid");
  check(value == (kind < 2    ? (valid ? 11 : 0)
                  : valid     ? 0
                  : kind == 3 ? -1
                              : -2),
        "failed row is not delivered, persisted as empty, or confused with a missing reference");
  if (!valid && !mode.ends_with("alloc")) check(faultHits != 0, "row read/seek fault reached query");
  return failures ? 1 : 0;
}
