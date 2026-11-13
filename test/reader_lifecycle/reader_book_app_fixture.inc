// Observe actual Rust book binding and close; native references belong only to comparison children.
class Epub;
extern "C" void xp_epub_test_book_observer(void (*)(void*, uint8_t, const XpEpubBookView*));
namespace {
struct ReaderFile { std::vector<uint8_t> bytes; size_t cursor = 0; std::string path; };
struct ReaderBookObservation { Handle* marker; std::weak_ptr<Epub> nativeReference; };
std::map<void*, ReaderFile> readerFiles;
std::map<std::string, std::vector<uint8_t>> readerSectionFiles;
unsigned readerSectionOpens = 0, readerSectionCloses = 0;
std::map<void*, ReaderBookObservation> readerBooks;
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
                  "open:file", "progress-read", "bookmarks-load", "clippings-load", "close:file"});
  assert(readerBooks.size() == 1 && readerFiles.size() == 1 && readerBuffers.empty() && !readerCacheLocked);
}
void bindReaderBook() {
  const auto pageFixture = legacyBook.section.page;
  const auto childFixture = legacyBook.children;
  legacyBook = unexpectedLegacyBookServices();
  legacyBook.section.page = pageFixture;
  legacyBook.children = childFixture;
  leaves.native_path_hash = [](XpAppText path) -> uint64_t { assert(text(path) == f().path); return 42; };
  static auto openWrite = leaves.file_open_write;
  leaves.file_open_write = [](XpAppText path, XpAppText origin) -> void* {
    if (text(path) != readerCache + "/progress.bin.tmp") return openWrite(path, origin);
    assert(f().renderDepth == 1 && !f().modelDepth);
    assert(text(path) == readerCache + "/progress.bin.tmp" && text(origin) == "PRG");
    f().physical("progress-save");
    auto* raw = new uint8_t;
    readerFiles.emplace(raw, ReaderFile{{}, 0, text(path)}); return raw;
  };
  static auto write = leaves.file_write;
  leaves.file_write = [](void* raw, const uint8_t* bytes, uint32_t size) -> uint32_t {
    if (!readerFiles.count(raw)) return write(raw, bytes, size);
    assert(bytes && (size == 6 || size == 10));
    auto& file = readerFiles.at(raw); assert(file.path == readerCache + "/progress.bin.tmp");
    file.bytes.assign(bytes, bytes + size); return size;
  };
  leaves.file_flush = [](void* raw) { assert(readerFiles.at(raw).path == readerCache + "/progress.bin.tmp"); };
  static auto remove = leaves.path_remove;
  leaves.path_remove = [](XpAppText path) -> uint8_t {
    if (text(path) == readerCache + "/progress.bin") return 1;
    return remove(path);
  };
  static auto rename = leaves.path_rename;
  leaves.path_rename = [](XpAppText from, XpAppText to) -> uint8_t {
    if (text(from) == readerCache + "/progress.bin.tmp") { assert(text(to) == readerCache + "/progress.bin"); return 1; }
    return rename(from, to);
  };
  resources.path_exists = [](XpAppText path) -> uint8_t {
    const auto value = text(path);
    return value == fixtureBookmarkPath() ||
           readerSectionFiles.count(value) || value == f().path || value == readerCache + "/book.bin" ||
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
  static auto mkdir = leaves.path_mkdir;
  leaves.path_mkdir = [](XpAppText path) -> uint8_t {
    const auto value = text(path);
    if (value == readerCache || value == readerCache + "/sections") return 1;
    return mkdir(path);
  };
  xp_epub_test_book_observer([](void* raw, uint8_t event, const XpEpubBookView* view) {
    if (event == 1) {
      auto found = readerBooks.find(raw);
      if (found == readerBooks.end()) return;
      assert(f().renderDepth == 1 && !f().extractor && !f().clippings);
      assert(found->second.nativeReference.expired());
      auto* marker = found->second.marker;
      readerBooks.erase(found); f().release(marker, Kind::Book); return;
    }
    assert(event == 0);
    if (!raw) {
      assert(f().extractor); ++f().clearedExtractor;
      f().physical("extractor-clear", f().extractor->generation); f().extractor = nullptr; return;
    }
    assert(view && view->spine_count == 7 && view->toc_count == 5 && text(view->cache_path) == readerCache);
    auto found = readerBooks.find(raw);
    if (found == readerBooks.end()) {
      auto* marker = f().acquire(Kind::Book, f().entersGeneration); marker->path = f().path;
      found = readerBooks.emplace(raw, ReaderBookObservation{marker, {}}).first;
      f().physical("extractor-install", marker->generation, marker->id);
    }
    assert(!f().extractor); f().extractor = found->second.marker;
  });
  clipping_fixture::appStoreRequested = true;
  clipping_fixture::preserveBookHash = true;
  clipping_fixture::unloaded = [](void) {
    assert(f().renderDepth == 1 && f().clippings);
    ++f().unloadedClippings;
    f().physical("clippings-unload", f().clippings->generation);
    f().clippings = nullptr;
  };
  clipping_fixture::loaded = [](const XpAppBook* row, uint8_t loaded) {
    assert(loaded);
    assert(row && text(row->path) == f().path && text(row->title) == "Fixture" && text(row->author) == "Author");
    assert(!f().clippings && f().extractor); f().clippings = f().extractor;
    f().physical("clippings-load", f().extractor->generation, f().extractor->id);
  };
}
}
