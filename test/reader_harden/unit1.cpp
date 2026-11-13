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
static size_t faultOffset, faultHits;
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
  if (mode == "seek") {
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
    } else if (mode == "cache-seek") {
      HalFileTest::allowSeek = [](const char*, size_t) {
        ++faultHits;
        return false;
      };
    } else if (mode == "open") {
      HalFileTest::allowOpen = [](const char*, const char*) {
        ++faultHits;
        return false;
      };
    }
    const bool accepted = cache.load();
    check(accepted == (mode == "valid"), "H2 load rejects damaged or unread cache");
    check(cache.isLoaded() == accepted, "H2 loaded flag matches result");
    if (mode == "valid") {
      check(cache.getSpineEntry(0).href == "chapter.xhtml", "H2 healthy spine");
      check(cache.getTocEntry(0).title == "Chapter", "H2 healthy TOC");
      std::ofstream(root + "/book.bin", std::ios::binary).put(10);
      check(!cache.load() && !cache.isLoaded(), "H2 reused owner rejects truncated reload");
    }
    if (mode == "read" || mode == "cache-seek" || mode == "open") check(faultHits != 0, "H2 fault reached");
  }
  return failures ? 1 : 0;
}
