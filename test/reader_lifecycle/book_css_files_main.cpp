#include <Epub/css/CssParser.h>

#include <fstream>
#include <iterator>

static size_t dedupFailureBytes = 0;
static bool dedupFailed = false;
static bool failDedup(size_t bytes) {
  if (dedupFailureBytes && bytes == dedupFailureBytes && !dedupFailed) {
    dedupFailed = true;
    return true;
  }
  return false;
}
// COMMON_ITEM_COMPOSITION

void* operator new[](size_t bytes, const std::nothrow_t&) noexcept {
  if (failDedup(bytes)) return nullptr;
  try {
    return ::operator new[](bytes);
  } catch (...) {
    return nullptr;
  }
}

static std::vector<size_t> targetPermutation;
static void hostOnlyTargetCssSort(XpBookCssEntry* entries, size_t count) {
  if (targetPermutation.empty()) {
    std::sort(entries, entries + count, [](const auto& a, const auto& b) { return a.pathHash < b.pathHash; });
    for (size_t i = 1; i < count; ++i) assert(entries[i - 1].pathHash != entries[i].pathHash);
    return;
  }
  assert(targetPermutation.size() == count);
  std::vector<XpBookCssEntry> original(entries, entries + count);
  for (size_t i = 0; i < count; ++i) entries[i] = original.at(targetPermutation[i]);
}
// ACTUAL_CSS_METHODS

extern "C" uint8_t xp_epub_test_css_sort(XpBookCssEntry*, size_t);
extern "C" uint32_t xp_epub_test_css_files(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText,
                                           const XpAppText*, size_t, XpAppText, uint8_t, uint8_t);
extern "C" size_t xp_test_live_allocations();
int main(int argc, char** argv) {
  assert(argc >= 9);
  const std::string mode = argv[1], path = argv[2], cache = argv[3], base = argv[7];
  const bool discover = std::stoul(argv[5]) != 0, partial = std::stoul(argv[6]) != 0;
  BookFault::failAt = std::stoul(argv[4]);
  ESP.freeHeap = std::strtoul(argv[8], nullptr, 10);
  std::vector<std::string> paths;
  for (int i = 9; i < argc; ++i) paths.emplace_back(argv[i]);
  if (std::getenv("CSS_DEDUP_FAIL")) dedupFailureBytes = paths.size() * sizeof(XpBookCssEntry);
  if (mode == "sort") {
    std::vector<XpBookCssEntry> entries;
    for (size_t i = 0; i < paths.size(); ++i)
      entries.push_back({ZipFile::fnvHash64(paths[i].data(), paths[i].size()), 0, i});
    assert(xp_epub_test_css_sort(entries.data(), entries.size()));
    for (const auto& entry : entries) printf("%zu ", entry.cssIndex);
    puts("");
    return 0;
  }
  if (const char* permutation = std::getenv("CSS_TARGET_PERMUTATION")) {
    std::ifstream input(permutation);
    size_t index;
    while (input >> index) targetPermutation.push_back(index);
  }
  uint32_t result;
  if (mode == "native") {
    std::string error;
    Epub epub{path, freeink::content::openProtectedBook(path, error)};
    epub.cachePath = cache;
    epub.contentBasePath = base;
    epub.cssFiles = paths;
    epub.cssParser = std::make_unique<CssParser>(cache);
    if (discover) epub.discoverCssFilesFromZip();
    result = static_cast<uint32_t>(
        epub.parseCssFiles(partial ? CssParser::CacheStatus::Partial : CssParser::CacheStatus::Missing));
    assert(epub.cssParser->rustHandle() == nullptr);
  } else {
    std::vector<XpAppText> borrowed;
    for (const auto& path : paths) borrowed.push_back(text(path));
    result = xp_epub_test_css_files(&resources, &leaves, text(path), text(cache), borrowed.data(), borrowed.size(),
                                    text(base), discover, partial);
  }
  assert(nativeBuffers.empty() && xp_test_live_allocations() == 0);
  printf("result %u\nsteps %zu\n", result, BookFault::step);
}
