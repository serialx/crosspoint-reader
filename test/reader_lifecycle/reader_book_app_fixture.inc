// Actual Rust book ownership with a native facade; only storage/UI are physical fakes.
extern "C" void* testNativeFacadeOpen(void*, const XpEpubBookView*, const XpEpubBookBorrow*);
extern "C" void testNativeFacadeClose(void*);
extern "C" void testNativeExtractor(void*);
namespace {
struct ReaderFile { std::vector<uint8_t> bytes; size_t cursor = 0; std::string path; };
struct ReaderFacade { Handle* marker; void* owner; const XpEpubBookBorrow* callbacks; };
std::map<void*, ReaderFile> readerFiles;
std::map<std::string, std::vector<uint8_t>> readerSectionFiles;
unsigned readerSectionOpens = 0, readerSectionCloses = 0;
std::map<void*, ReaderFacade> readerFacades;
std::map<void*, size_t> readerBuffers;
bool readerCacheLocked = false;
const std::string readerCache = "/.crosspoint/epub_42";
void number(std::vector<uint8_t>& out, uint32_t value, unsigned width) {
  for (unsigned i = 0; i < width; ++i) out.push_back(uint8_t(value >> (8 * i)));
}
void stringBytes(std::vector<uint8_t>& out, const std::string& value) {
  number(out, uint32_t(value.size()), 4); out.insert(out.end(), value.begin(), value.end());
}
std::vector<uint8_t> readerCacheBytes() {
  std::vector<uint8_t> metadata;
  for (const char* value : {"Fixture", "Author", "en", "", ""}) stringBytes(metadata, value);
  std::vector<std::vector<uint8_t>> rows;
  const uint32_t sizes[] = {100, 300, 500, 600, 700, 800, 1000};
  for (unsigned i = 0; i < 7; ++i) {
    std::vector<uint8_t> row;
    stringBytes(row, i == 2 ? "chapter.xhtml" : i == 3 ? "target.xhtml" : "chapter" + std::to_string(i) + ".xhtml");
    number(row, sizes[i], 4); number(row, 0, 2); rows.push_back(std::move(row));
  }
  for (unsigned i = 0; i < 5; ++i) {
    std::vector<uint8_t> row;
    stringBytes(row, "Chapter"); stringBytes(row, "chapter.xhtml"); stringBytes(row, "anchor");
    number(row, 1, 1); number(row, 2, 2); rows.push_back(std::move(row));
  }
  const uint32_t table = 9 + uint32_t(metadata.size());
  std::vector<uint8_t> bytes;
  number(bytes, 10, 1); number(bytes, table, 4); number(bytes, 7, 2); number(bytes, 5, 2);
  bytes.insert(bytes.end(), metadata.begin(), metadata.end());
  uint32_t offset = table + 4 * uint32_t(rows.size());
  for (const auto& row : rows) { number(bytes, offset, 4); offset += uint32_t(row.size()); }
  for (const auto& row : rows) bytes.insert(bytes.end(), row.begin(), row.end());
  return bytes;
}
void assertReaderEntered() {
  assert(xp_app_test_current_epub() && f().entered == 1 && !f().exited);
  assert(f().extractor && f().clippings == f().extractor);
  assert(f().bookLoads == 1 && f().bookmarkLoads == 1 && f().progressReads == 1);
  f().count(Kind::File, 1, 1); f().count(Kind::Book, 1, 0);
  f().ordered(1, {"entry", "fonts-load", "cache-read", "open:book", "extractor-install",
                  "open:file", "progress-read", "bookmarks-clear", "bookmarks-load", "clippings-load", "close:file"});
  assert(readerFacades.size() == 1 && readerFiles.size() == 1 && readerBuffers.empty() && !readerCacheLocked);
}
void bindReaderBook() {
  legacyBook = unexpectedLegacyBookServices();
  leaves.native_path_hash = [](XpAppText path) -> uint64_t { assert(text(path) == f().path); return 42; };
  resources.path_exists = [](XpAppText path) -> uint8_t {
    const auto value = text(path);
    return readerSectionFiles.count(value) || value == f().path || value == readerCache + "/book.bin" ||
           value == readerCache + "/css_rules.cache" || value == readerCache + "/html/0.html";
  };
  resources.buffer_allocate = [](uint8_t, size_t size) -> uint8_t* {
    auto* value = static_cast<uint8_t*>(std::malloc(size)); assert(value); readerBuffers[value] = size; return value;
  };
  resources.buffer_release = [](uint8_t, uint8_t* raw) { assert(readerBuffers.erase(raw) == 1); std::free(raw); };
  leaves.protection_open = [](XpAppText path, XpAppTextSink, void*) -> void* { assert(text(path) == f().path); return nullptr; };
  leaves.cache_lock = [] { assert(!readerCacheLocked && !f().modelDepth); readerCacheLocked = true; };
  leaves.cache_unlock = [] { assert(readerCacheLocked); readerCacheLocked = false; };
  static auto progressRead = leaves.file_read;
  static auto progressClose = leaves.file_close;
  leaves.file_open_read = [](XpAppText path, XpAppText origin) -> void* {
    const auto value = text(path);
    if (value == readerCache + "/progress.bin") {
      assert(text(origin) == "ERS" && f().extractor && !f().renderDepth);
      return f().acquire(Kind::File, f().extractor->generation);
    }
    std::vector<uint8_t> bytes;
    if (readerSectionFiles.count(value)) {
      assert(text(origin) == "SCT" && !f().modelDepth);
      bytes = readerSectionFiles.at(value); ++readerSectionOpens;
      std::fprintf(stderr, "section-file-open path=%s bytes=%zu\n", value.c_str(), bytes.size());
    } else if (value == readerCache + "/book.bin") {
      bytes = readerCacheBytes(); ++f().bookLoads; f().physical("cache-read", f().entersGeneration);
    } else if (value == readerCache + "/css_rules.cache") {
      bytes = {19, 0, 0, 0};
    } else if (value == readerCache + "/html/0.html") {
      const std::string tags = "<!-- TXT_CACHE_VERSION: 1 --><!-- MD_CACHE_VERSION: 1 -->";
      bytes.assign(tags.begin(), tags.end());
    } else if (value == f().path) {
      bytes.resize(100, 'x');
    } else {
      assert(value == readerCache + "/links.bin" || value == readerCache + "/css_rules.cache.bak"); return nullptr;
    }
    auto* raw = new uint8_t;
    readerFiles.emplace(raw, ReaderFile{std::move(bytes), 0, value}); return raw;
  };
  leaves.file_read = [](void* raw, uint8_t* bytes, uint32_t size) -> int32_t {
    auto it = readerFiles.find(raw); if (it == readerFiles.end()) return progressRead(raw, bytes, size);
    auto& file = it->second; const auto count = std::min(size_t(size), file.bytes.size() - std::min(file.cursor, file.bytes.size()));
    if (count) std::memcpy(bytes, file.bytes.data() + file.cursor, count); file.cursor += count; return int32_t(count);
  };
  leaves.file_seek = [](void* raw, uint32_t offset) -> uint8_t { readerFiles.at(raw).cursor = offset; return 1; };
  leaves.file_position = [](void* raw) -> uint32_t { return uint32_t(readerFiles.at(raw).cursor); };
  leaves.file_size = [](void* raw) -> uint32_t { return uint32_t(readerFiles.at(raw).bytes.size()); };
  leaves.file_close = [](void* raw) -> uint8_t {
    const auto it = readerFiles.find(raw);
    if (it != readerFiles.end()) {
      if (readerSectionFiles.count(it->second.path)) {
        assert(!f().modelDepth); ++readerSectionCloses;
        std::fprintf(stderr, "section-file-close path=%s cursor=%zu\n", it->second.path.c_str(), it->second.cursor);
      }
      readerFiles.erase(it); delete static_cast<uint8_t*>(raw); return 1;
    }
    return progressClose(raw);
  };
  leaves.path_mkdir = [](XpAppText path) -> uint8_t {
    const auto value = text(path); assert(value == readerCache || value == readerCache + "/sections"); return 1;
  };
  leaves.book_facade_open = [](void* owner, const XpEpubBookView* view, const XpEpubBookBorrow* callbacks) -> void* {
    assert(view->spine_count == 7 && view->toc_count == 5 && text(view->cache_path) == readerCache);
    void* raw = testNativeFacadeOpen(owner, view, callbacks); assert(raw);
    auto* marker = f().acquire(Kind::Book, f().entersGeneration); marker->path = f().path;
    readerFacades.emplace(raw, ReaderFacade{marker, owner, callbacks});
    assert(!f().extractor); f().extractor = marker;
    f().physical("extractor-install", marker->generation, marker->id); return raw;
  };
  leaves.book_set_extractor = [](void* raw) {
    testNativeExtractor(raw);
    if (raw) { auto* marker = readerFacades.at(raw).marker; assert(!f().extractor); f().extractor = marker; }
    else { assert(f().extractor); ++f().clearedExtractor; f().physical("extractor-clear", f().extractor->generation); f().extractor = nullptr; }
  };
  leaves.book_facade_close = [](void* raw) {
    assert(f().renderDepth == 1 && !f().extractor && !f().clippings);
    auto* marker = readerFacades.at(raw).marker; testNativeFacadeClose(raw); readerFacades.erase(raw); f().release(marker, Kind::Book);
  };
  leaves.bookmarks_load = [](void* raw, XpAppText path) -> uint8_t {
    auto& marker = f().handle(raw, Kind::Bookmarks); assert(text(path) == f().path && !f().renderDepth);
    ++f().bookmarkLoads; f().physical("bookmarks-load", marker.generation, marker.id); return 1;
  };
  leaves.clippings_load = [](const XpAppBook* row) -> uint8_t {
    assert(row && text(row->path) == f().path && text(row->title) == "Fixture" && text(row->author) == "Author");
    assert(!f().clippings && f().extractor); f().clippings = f().extractor;
    f().physical("clippings-load", f().extractor->generation, f().extractor->id); return 1;
  };
}
}
