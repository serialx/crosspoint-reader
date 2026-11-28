#include <Epub/BookMetadataCache.h>
#include <Epub/css/CssParser.h>
#include <Epub/parsers/PackageParser.h>
#include <Txt.h>
#include <Utf8.h>

#include <optional>
// COMMON_ITEM_COMPOSITION
// ACTUAL_LOAD_METHODS
extern "C" {
using ObserveBook = void (*)(void*, uint32_t, int32_t, XpAppText, XpAppText, XpAppText, uint32_t, int32_t);
uint8_t xp_epub_test_load(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText, uint8_t, uint8_t, uint32_t,
                          ObserveBook, void*);
size_t xp_test_live_allocations();
}
// Query bytes use a dedicated pipe: Python hashes them without retaining a hex dump.
static FILE* queryStream;
static uint64_t queryRecords = 0, queryBytes = 0;
static void queryInteger(uint64_t value) {
  uint8_t bytes[8];
  for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = value >> (8 * i);
  assert(fwrite(bytes, 1, sizeof(bytes), queryStream) == sizeof(bytes));
  queryBytes += sizeof(bytes);
}
static void observe(void*, uint32_t kind, int32_t index, XpAppText a, XpAppText b, XpAppText c, uint32_t first,
                    int32_t second) {
  if (kind == 9) {
    puts("queries");
    return;
  }
  ++queryRecords;
  for (uint64_t value : {uint64_t(kind), uint64_t(uint32_t(index)), uint64_t(first), uint64_t(uint32_t(second)),
                         uint64_t(a.len), uint64_t(b.len), uint64_t(c.len)})
    queryInteger(value);
  for (auto value : {a, b, c}) {
    if (value.len) assert(fwrite(value.data, 1, value.len, queryStream) == value.len);
    queryBytes += value.len;
  }
  if (kind == 8) printf("query-cap %u %d\n", first, second);
}
int main(int argc, char** argv) {
  assert(argc == 8);
  const bool native = std::string(argv[1]) == "native", build = std::stoul(argv[4]) != 0,
             skip = std::stoul(argv[5]) != 0;
  const std::string path = argv[2], cache = argv[3];
  BookFault::failAt = std::stoul(argv[6]);
  const uint32_t queryCap = std::stoul(argv[7]);
  assert(queryCap > 0);
  queryStream = fdopen(std::stoi(std::getenv("XP_BOOK_QUERY_FD")), "wb");
  assert(queryStream);
  bool result;
  if (native) {
    Epub epub;
    epub.filepath = path;
    epub.cachePath = cache;
    result = epub.load(build, skip);
    if (result) {
      puts("queries");
      auto& owner = *epub.bookMetadataCache;
      const auto& meta = owner.coreMetadata;
      unsigned index = 0;
      for (const auto* value :
           {&meta.title, &meta.author, &meta.language, &meta.coverItemHref, &meta.textReferenceHref})
        observe(nullptr, 0, index++, text(*value), {}, {}, 0, 0);
      observe(nullptr, 1, 0, {}, {}, {}, owner.getSpineCount(), owner.getTocCount());
      const int spineLimit = std::min<uint32_t>(owner.getSpineCount(), queryCap);
      const int tocLimit = std::min<uint32_t>(owner.getTocCount(), queryCap);
      if (spineLimit != owner.getSpineCount() || tocLimit != owner.getTocCount())
        observe(nullptr, 8, 0, {}, {}, {}, spineLimit, tocLimit);
      for (int i = 0; i < spineLimit; ++i) {
        const auto row = owner.getSpineEntry(i);
        observe(nullptr, 2, i, text(row.href), {}, {}, row.cumulativeSize, row.tocIndex);
      }
      for (int i = 0; i < tocLimit; ++i) {
        const auto row = owner.getTocEntry(i);
        observe(nullptr, 3, i, text(row.title), text(row.href), text(row.anchor), row.level, row.spineIndex);
      }
      for (int i = -1; i <= spineLimit; ++i) {
        const auto size = owner.getCumulativeSize(i);
        observe(nullptr, 4, i, {}, {}, {}, size, owner.getSpineIndexForSize(size));
      }
    }
  } else {
    result = xp_epub_test_load(&resources, &leaves, text(path), text(cache), build, skip, queryCap, observe, nullptr);
  }
  assert(fclose(queryStream) == 0);
  printf("query-summary %llu %llu\n", static_cast<unsigned long long>(queryRecords),
         static_cast<unsigned long long>(queryBytes));
  assert(xp_test_live_allocations() == 0 && nativeBuffers.empty());
  printf("result %u\nsteps %zu\n", result, BookFault::step);
}
