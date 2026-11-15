static void writerBoundary(const std::string& root) {
  WriterFixture::WriterStorage store;
  const auto io = store.io();
  XpPkgCache* writer = nullptr;
  assert(xp_pkg_cache_create(&writer) == 0);
  assert(xp_pkg_cache_control(writer, &io, 0, nullptr) == 0);
  for (const std::string href : {"a", "b", "c"})
    assert(xp_pkg_cache_entry(writer, &io, 0, {}, {reinterpret_cast<const uint8_t*>(href.data()), href.size()}, {}, 0,
                              nullptr) == 0);
  for (uint32_t operation : {1, 2, 3, 4}) assert(xp_pkg_cache_control(writer, &io, operation, nullptr) == 0);
  XpPkgSpan fields[5]{};
  assert(xp_pkg_cache_build(writer, &io, fields, nullptr) == 0);
  xp_pkg_cache_destroy(writer);
  const auto& bytes = store.files[XP_PKG_BOOK_FILE].bytes;
  {
    std::ofstream file(root + "/book.bin", std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  BookMetadataCache cache(root);
  assert(cache.load());
  assert(cache.getSpineCount() == 3 && cache.getTocCount() == 0);
  assert(cache.getSpineEntry(0).cumulativeSize == 0xffffffffu);
  assert(cache.getSpineEntry(1).cumulativeSize == 0);
  assert(cache.getSpineEntry(2).cumulativeSize == 1);
  std::ifstream file(root + "/book.bin", std::ios::binary);
  const std::vector<uint8_t> after{std::istreambuf_iterator<char>(file), {}};
  assert(after == bytes);
}
