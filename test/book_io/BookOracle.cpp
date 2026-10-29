#include <Epub.h>
#include <HalStorage.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
namespace {
size_t bookAllocationAttempts = 0;
void* bookNothrowAllocate(size_t size, bool array) noexcept {
  const char* limit = std::getenv("BOOK_CPP_FAIL");
  const bool fail = limit && std::strtoull(limit, nullptr, 10) == ++bookAllocationAttempts;
  if (!limit) ++bookAllocationAttempts;
  printf("allocation %zu %zu %u%s\n", bookAllocationAttempts, size, array, fail ? " FAIL" : "");
  if (fail) return nullptr;
  try {
    return array ? ::operator new[](size) : ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}
}  // namespace
void* operator new(size_t size, const std::nothrow_t&) noexcept { return bookNothrowAllocate(size, false); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return bookNothrowAllocate(size, true); }

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  BookFault::failAt = std::strtoull(argv[2], nullptr, 10);
  std::filesystem::create_directories("cache");
  Epub epub(argv[1], "cache");
  const bool loaded = epub.load();
  printf("result %u %d %d %zu\n", loaded, epub.getSpineItemsCount(), epub.getTocItemsCount(), epub.getBookSize());
  if (loaded) {
    printf("metadata %s|%s|%s\n", epub.getTitle().c_str(), epub.getAuthor().c_str(), epub.getLanguage().c_str());
    for (int i = -1; i <= epub.getSpineItemsCount(); ++i) {
      auto item = epub.getSpineItem(i);
      printf("spine %d %s %u %d %.9g\n", i, item.href.c_str(), item.cumulativeSize, item.tocIndex,
             epub.calculateProgress(i, 0.5f));
    }
    for (int i = -1; i <= epub.getTocItemsCount(); ++i) {
      auto item = epub.getTocItem(i);
      printf("toc %d %s|%s|%s %u %d\n", i, item.title.c_str(), item.href.c_str(), item.anchor.c_str(), item.level,
             item.spineIndex);
    }
  }
  std::string title, author;
  const bool metadata = epub.loadMetadata(title, author);
  printf("metadata-only %u %s|%s\n", metadata, title.c_str(), author.c_str());
  Epub::SyncMetadata sync;
  const bool synced = epub.loadSyncMetadata(sync);
  printf("sync %u %s|%s|%s %.9g\n", synced, sync.isbn.c_str(), sync.asin.c_str(), sync.series.c_str(),
         sync.seriesIndex.value_or(-9999));
  printf("warm %u\n", epub.load(false, false));
  printf("skip-css %u\n", epub.load(false, true));
  if (epub.getCssParser()) epub.getCssParser()->deleteCache();
  printf("rebuild-css %u\n", epub.load(false, false));
  printf("cover %u\n", epub.generateCoverBmp());
  printf("cover-cropped %u\n", epub.generateCoverBmp(true, true));
  printf("thumb %u\n", epub.generateThumbBmp(120));
  printf("source-thumb %u\n", epub.generateThumbBmpFromSource(240));
  printf("cover-warm %u\n", epub.generateCoverBmp());
  for (const std::string href : {"", "secret"}) {
    for (bool trailing : {false, true}) {
      size_t size = 12345;
      uint8_t* bytes = epub.readItemContentsToBytes(href, &size, trailing);
      printf("bytes %s %u %u %zu", href.c_str(), trailing, bytes != nullptr, size);
      if (bytes)
        for (size_t i = 0; i < size + trailing; ++i) printf(" %02x", bytes[i]);
      puts("");
      free(bytes);
    }
    struct ShortWriter : Print {
      size_t used = 0;
      size_t write(uint8_t) override { return used++ < 5 ? 1 : 0; }
    } writer;
    printf("stream %s %u\n", href.c_str(), epub.readItemContentsToStream(href, writer, 1024, true));
  }
  printf("extract-secret %u\n", epub.extractItemToFile("secret", "cache/secret.out"));
  printf("protection-error %s\n", epub.getProtectionError().c_str());
  if (std::getenv("BOOK_CLEAR_CACHE")) {
    printf("clear-cache %u\n", epub.clearCache());
    printf("clear-cache-again %u\n", epub.clearCache());
    epub.setupCacheDir();
    printf("clear-recreated-cache %u\n", epub.clearCache());
  }
  printf("allocations %zu\n", bookAllocationAttempts);
  printf("steps %zu\n", BookFault::step);
}
