#include <Epub.h>
#include <HalStorage.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
  printf("steps %zu\n", BookFault::step);
}
