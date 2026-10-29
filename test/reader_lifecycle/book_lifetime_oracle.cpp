#include <xp_ui_reader.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#define LOG_DBG(tag, fmt, ...) std::printf(tag " " fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(tag, fmt, ...) std::printf(tag " " fmt "\n", ##__VA_ARGS__)
static void event(const char* name, int a = 0, int b = 0) { std::printf("%s %d %d\n", name, a, b); }
struct Config {
  bool allocate = true, load = true, cached = true, file = true, clippings = true;
  int readSize = 10, textSpine = 2;
  std::string error;
  std::vector<uint8_t> bytes = {0, 0, 2, 0, 12, 0, 0x12, 0x34, 0x56, 0x78, 0, 0};
} config;
struct GfxRenderer {
  bool framebuffer = true;
  struct FrameBufferLoan {
    GfxRenderer& renderer;
    explicit FrameBufferLoan(GfxRenderer& r) : renderer(r) {
      assert(r.framebuffer);
      r.framebuffer = false;
      event("loan");
    }
    ~FrameBufferLoan() {
      assert(!renderer.framebuffer);
      renderer.framebuffer = true;
      event("return-loan");
    }
  };
};
enum { STR_INDEXING };
const char* tr(int) { return "indexing"; }
struct {
  void drawPopup(GfxRenderer& r, const char* text) {
    assert(r.framebuffer);
    std::printf("popup [%s]\n", text);
  }
} GUI;
struct {
  uint8_t embeddedStyle = 0;
} SETTINGS;
struct Epub {
  Epub(const std::string& path, const char* cache) { std::printf("epub-new [%s] [%s]\n", path.c_str(), cache); }
  ~Epub() { event("epub-drop"); }
  std::string getPath() const { return "/book.epub"; }
  std::string getCachePath() const { return "/cache"; }
  std::string getTitle() const { return "Title"; }
  std::string getAuthor() const { return "Author"; }
  bool load(bool cache, bool style) {
    event("load", cache, style);
    return config.load;
  }
  std::string getProtectionError() const { return config.error; }
  bool extractItemToFile(const char* source, const char* dest) {
    std::printf("extract [%s] [%s]\n", source, dest);
    return true;
  }
  void setupCacheDir() { event("setup-cache"); }
  int getSpineIndexForTextReference() const {
    event("text-reference", config.textSpine);
    return config.textSpine;
  }
};
template <class T, class... A>
std::unique_ptr<T> makeUniqueNoThrow(A&&... args) {
  event("allocate", config.allocate);
  return config.allocate ? std::make_unique<T>(std::forward<A>(args)...) : nullptr;
}
struct ImageBlock {
  static inline void* context = nullptr;
  static inline bool (*extractor)(void*, const char*, const char*) = nullptr;
  static void clearRenderFailures() { event("clear-image-failures"); }
  static void setExtractor(void* ctx, bool (*fn)(void*, const char*, const char*)) {
    context = ctx;
    extractor = fn;
    event("set-extractor", ctx != nullptr, fn != nullptr);
  }
};
struct HalFile {
  bool open = false;
  int read(uint8_t* out, size_t capacity) {
    const int n = std::min(config.readSize, static_cast<int>(capacity));
    if (n > 0) memcpy(out, config.bytes.data(), n);
    event("file-read", n);
    return n;
  }
  ~HalFile() {
    if (open) event("file-close");
  }
};
struct {
  bool exists(const char* path) {
    std::printf("exists [%s] %d\n", path, config.cached);
    return config.cached;
  }
  bool openFileForRead(const char* tag, const std::string& path, HalFile& file) {
    std::printf("open [%s] [%s] %d\n", tag, path.c_str(), config.file);
    file.open = config.file;
    return config.file;
  }
} Storage;
struct {
  void unload() { event("clippings-unload"); }
  bool loadForBook(const std::string& path, const std::string& title, const std::string& author, const char* kind) {
    std::printf("clippings-load [%s] [%s] [%s] [%s] %d\n", path.c_str(), title.c_str(), author.c_str(), kind,
                config.clippings);
    return config.clippings;
  }
} CLIPPINGS;
namespace bleturner {
void afterPaint() { event("radio-after"); }
}  // namespace bleturner
std::string buildReadFolderDestination(const std::string& path) {
  std::printf("destination [%s]\n", path.c_str());
  return "/Read/book.epub";
}
void moveFinishedBookToReadFolder(const std::string& source, const std::string& dest, const std::string& cache) {
  std::printf("move [%s] [%s] [%s]\n", source.c_str(), dest.c_str(), cache.c_str());
}
struct Section {
  ~Section() { event("section-drop"); }
};
struct EpubReaderActivity {
  std::string bookPath = "/book.epub", loadProtectionError;
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section;
  GfxRenderer renderer;
  std::atomic<bool> radioReleasedForBuild{false};
  int footnoteDepth = 0, currentSpineIndex = 0, nextPageNumber = 7, cachedSpineIndex = 9,
      cachedChapterTotalPageCount = 6;
  std::optional<uint32_t> cachedVisibleTextOffset = 99;
  bool pendingReadFolderMove = false;
  void settleOverlayRefresh() { event("settle"); }
  void discardOverlayPage() { event("discard"); }
  void saveLinkStack() { event("save-links"); }
  void loadLinkStack() { event("load-links"); }
  void loadCachedBookmarks() { event("load-bookmarks"); }
  void disableFastInitialRefresh() { event("disable-fast"); }
  bool loadBook();
  ~EpubReaderActivity();
  void dump(bool loaded) const {
    std::printf("state %d %d %d %d %d %d %d %u [%s]\n", loaded, bool(epub), currentSpineIndex, nextPageNumber,
                cachedSpineIndex, cachedChapterTotalPageCount, bool(cachedVisibleTextOffset),
                cachedVisibleTextOffset.value_or(0), loadProtectionError.c_str());
  }
};
// LIFETIME_METHODS
int main() {
  for (int size : {-1, 0, 3, 4, 5, 6, 9, 10, 12})
    for (int flags = 0; flags < 64; ++flags) {
      config = {};
      config.readSize = size;
      config.allocate = !(flags & 1);
      config.load = !(flags & 2);
      config.cached = flags & 4;
      config.file = flags & 8;
      config.clippings = !(flags & 16);
      config.error = flags & 32 ? "access expired" : "";
      config.textSpine = flags & 16 ? 0 : 2;
      config.bytes[0] = flags & 32 ? 2 : 0;
      if (flags & 16) config.bytes[2] = config.bytes[3] = 255;
      SETTINGS.embeddedStyle = flags & 4 ? 1 : 0;
      std::printf("load-case %d %d\n", size, flags);
      EpubReaderActivity a;
      const bool loaded = a.loadBook();
      a.dump(loaded);
      assert(a.renderer.framebuffer);
      if (ImageBlock::extractor) assert(ImageBlock::extractor(ImageBlock::context, "image.png", "/image"));
    }
  for (int flags = 0; flags < 32; ++flags) {
    std::printf("destroy-case %d\n", flags);
    EpubReaderActivity a;
    if (flags & 1) a.epub = std::make_shared<Epub>("/book.epub", "/.crosspoint");
    if (flags & 2) a.section = std::make_unique<Section>();
    a.radioReleasedForBuild = flags & 4;
    a.footnoteDepth = flags & 8 ? 2 : 0;
    a.pendingReadFolderMove = flags & 16;
  }
}
