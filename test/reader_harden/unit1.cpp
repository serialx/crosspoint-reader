#include <xp_book.h>
#include <xp_book_residual.h>

#include <fstream>
#include <vector>

extern "C" uint8_t xp_epub_test_txt_validate(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText,
                                             uint32_t);
extern "C" int64_t xp_epub_test_zip(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText, uint8_t,
                                    XpBookSink, void*);
static std::vector<uint8_t> output;
static uint8_t receive(void*, const uint8_t* bytes, size_t count) {
  output.insert(output.end(), bytes, bytes + count);
  return 1;
}
static size_t faultOffset, faultHits, reads, seeks, bytesRead;
static int failures;
static void check(bool safe, const char* name) {
  printf("%s: %s\n", name, safe ? "PASS" : "FAIL");
  failures += !safe;
}
int main(int argc, char** argv) {
  assert(argc == 4);
  const std::string mode = argv[1], root = argv[2];
  const auto offset = std::stoul(argv[3]);
  auto resources = unexpectedAppResources();
  resources.buffer_allocate = [](XpAppMemoryDomain, size_t n) { return static_cast<uint8_t*>(std::malloc(n)); };
  resources.buffer_release = [](XpAppMemoryDomain, uint8_t* p) { std::free(p); };
  resources.path_exists = [](XpAppText p) -> uint8_t { return Storage.exists(pathText(p)); };
  auto leaves = cacheLeaves();
  leaves.path_remove_tree = [](XpAppText p) -> uint8_t { return std::filesystem::remove_all(owned(p)) != 0; };
  leaves.heap_snapshot = [](XpEpubHeap* h) {
    *h = {};
    h->free_bytes = 1000000;
  };
  if (mode == "writer") {
    writerBoundary(root);
  } else if (mode == "policy") {
    struct State {
      int loads = 0, builds = 0;
    } state;
    XpBookServices services{&state,
                            [](void* raw, uint32_t op, uint32_t) -> uint32_t {
                              auto& state = *static_cast<State*>(raw);
                              if (op == XP_BOOK_LOAD_METADATA) {
                                ++state.loads;
                                return 0;
                              }
                              if (op == XP_BOOK_IS_TEXT) return 0;
                              if (op == XP_BOOK_BUILD_BOOK) {
                                assert(++state.builds == 1);
                              }
                              return 1;
                            },
                            [](void*) -> uint32_t { return 0; }, nullptr};
    check(!xp_book_load(&services, 1, 1), "H2 fresh-cache rejection fails open");
    check(state.loads == 2 && state.builds == 1, "H2 at most one rebuild per open");
  } else if (mode == "seek") {
    const auto path = root + "/source.epub";
    check(xp_epub_test_zip(&resources, &leaves, text(path), text("chapter.xhtml"), 1, receive, nullptr) == 1,
          "S3 healthy extraction");
    check(std::string(output.begin(), output.end()) == "HELLO READERS", "S3 healthy bytes");
    output.clear();
    faultOffset = offset;
    HalFileTest::allowSeek = [](const char*, size_t at) {
      if (at == faultOffset) {
        ++faultHits;
        return false;
      }
      return true;
    };
    check(xp_epub_test_zip(&resources, &leaves, text(path), text("chapter.xhtml"), 1, receive, nullptr) == 0,
          "S3 reject failed data seek");
    check(faultHits == 1 && output.empty(), "S3 no bytes delivered after failed seek");
    HostFile file;
    assert(file.open(path.c_str(), "rb"));
    uint8_t name[256]{};
    XpBookZipIndexServices index{&file,
                                 [](void*, uint32_t op, uint32_t) -> size_t {
                                   assert(op == XP_BOOK_ZIP_INDEX_OPEN || op == XP_BOOK_ZIP_INDEX_CACHE_SIZE);
                                   return 1;
                                 },
                                 [](void* raw, size_t at) { return static_cast<HostFile*>(raw)->seek(at); },
                                 [](void* raw, uint8_t* bytes, size_t count) -> int32_t {
                                   return static_cast<HostFile*>(raw)->read(bytes, count);
                                 },
                                 [](void*, uint32_t op, XpZipEntry* entry, XpBookBorrow) {
                                   assert(op == XP_BOOK_ZIP_CACHE_FIND);
                                   *entry = {};
                                   entry->compressed_size = entry->uncompressed_size = 12;
                                   return true;
                                 },
                                 name,
                                 nullptr};
    auto extraction = poisoned<XpBookZipServices>();
    size_t size = 0;
    check(!xp_book_zip_memory(&extraction, &index, {}, {}, false, &size), "S3 native memory rejects data seek");
    check(!xp_book_zip_stream(&extraction, &index, {}, {}, 16, false), "S3 native stream rejects data seek");
    check(faultHits == 3, "S3 both native data seek faults reached");
  } else if (mode == "text" || mode == "text-empty") {
    const bool valid = xp_epub_test_txt_validate(&resources, &leaves, text(root + "/source.txt"), text(root), 0);
    check(valid == (mode == "text-empty"), "M1 compare empty cached source size");
  } else {
    BookMetadataCache cache(root);
    if (mode == "read") {
      faultOffset = offset;
      HalFileTest::readLimit = [](const char*, size_t at, size_t count) -> size_t {
        if (at == faultOffset && count) {
          ++faultHits;
          return count - 1;
        }
        return count;
      };
    } else if (mode == "open") {
      HalFileTest::allowOpen = [](const char*, const char*) {
        ++faultHits;
        return false;
      };
    }
    if (mode == "cost") {
      HalFileTest::readLimit = [](const char*, size_t, size_t count) {
        ++reads;
        bytesRead += count;
        return count;
      };
      HalFileTest::allowSeek = [](const char*, size_t) {
        ++seeks;
        return true;
      };
    }
    const bool accepted = cache.load();
    if (mode == "cost") printf("OPEN_IO reads=%zu seeks=%zu bytes=%zu\n", reads, seeks, bytesRead);
    check(accepted == (mode == "valid" || mode == "boundary" || mode == "cost" || mode == "cache-seek"),
          "H2 load rejects damaged or unread cache");
    check(cache.isLoaded() == accepted, "H2 loaded flag matches result");
    if (mode == "valid") {
      check(cache.getSpineEntry(0).href == "chapter.xhtml", "H2 healthy spine");
      check(cache.getTocEntry(0).title == "Chapter", "H2 healthy TOC");
      std::ofstream(root + "/book.bin", std::ios::binary).put(10);
      check(!cache.load() && !cache.isLoaded(), "H2 reused owner rejects truncated reload");
    }
    if (mode == "boundary" && cache.getSpineCount())
      check(cache.getSpineEntry(cache.getSpineCount() - 1).cumulativeSize == offset, "H2 writer size boundary");
    if (mode == "cache-seek") {
      HalFileTest::allowSeek = [](const char*, size_t) {
        ++faultHits;
        return false;
      };
      check(cache.getSpineEntry(0).href.empty(), "H2 query rejects failed seek");
    }
    if (mode == "read" || mode == "cache-seek" || mode == "open") check(faultHits != 0, "H2 fault reached");
  }
  return failures ? 1 : 0;
}
