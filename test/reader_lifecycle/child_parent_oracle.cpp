#include <xp_ui_reader.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
static int lockDepth = 0, modelDepth = 0;
static void event(const char* n, int a = 0, int b = 0) {
  assert(!modelDepth);
  std::printf("%s %d %d lock=%d\n", n, a, b, lockDepth);
}
#ifdef CHILD_RELEASE_LOG
#define LOG_DBG(...) ((void)0)
#else
#define LOG_DBG(tag, fmt, ...) (assert(!modelDepth), std::printf(tag " " fmt "\n", ##__VA_ARGS__))
#endif
#define LOG_ERR(tag, fmt, ...) (assert(!modelDepth), std::printf(tag " " fmt "\n", ##__VA_ARGS__))
struct RenderLock {
  RenderLock() {
    assert(!modelDepth);
    ++lockDepth;
    event("lock");
  }
  ~RenderLock() {
    event("unlock");
    --lockDepth;
  }
};
static bool allocationOk = true, saveOk = true, exportOk = true, removeOk = true;
static int loadLimit = 99, loads = 0, hit = -1, toc = 1;
uint32_t millis() {
  event("clock");
  return 123456;
}
struct Page {
  int number;
  explicit Page(int n) : number(n) { event("page-new", n); }
  ~Page() { event("page-drop", number); }
};
struct Cache {
  void releaseSdFontCaches() { event("release-fonts"); }
};
struct GfxRenderer {
  Cache cache;
  bool hasCache = true;
  void getOrientedViewableTRBL(int* t, int* r, int* b, int* l) const {
    event("margins");
    *t = 3;
    *r = 4;
    *b = 5;
    *l = 6;
  }
  Cache* getFontCacheManager() {
    event("cache", hasCache);
    return hasCache ? &cache : nullptr;
  }
  struct FrameBufferLoan {
    explicit FrameBufferLoan(GfxRenderer&) {
      assert(lockDepth == 1);
      event("loan");
    }
    ~FrameBufferLoan() { event("loan-drop"); }
  };
};
struct MappedInputManager {};
struct Settings {
  char dictionaryName[8] = "Oracle";
  int screenMargin = 2;
  uint32_t readerRenderSpec(int w, int h) {
    event("spec", w, h);
    return 77;
  }
} SETTINGS;
uint32_t readerRenderSpecSignature(uint32_t n) {
  event("signature", n);
  return n;
}
struct CrossPointPosition {
  int marker = 31;
};
struct SavedProgressPosition {
  std::string xpath;
  float percentage = 0;
};
struct Epub {
  struct Toc {
    std::string title = "Chapter title with enough bytes for a real string allocation";
  };
  ~Epub() { event("epub-drop"); }
  int getTocIndexForSpineIndex(int spine) {
    event("toc", spine);
    return toc;
  }
  Toc getTocItem(int i) {
    event("toc-item", i);
    return {};
  }
  std::string getTitle() {
    event("title");
    return "Book title with enough bytes for a real string allocation";
  }
  std::string getAuthor() {
    event("author");
    return "Author with enough bytes for a real string allocation";
  }
  std::string getPath() {
    event("path");
    return "/long/original/path/to/book.epub";
  }
};
struct Section {
  int currentPage = 2;
  uint16_t pageCount = 6;
  bool paragraph = true;
  ~Section() { event("section-drop"); }
  int estimatedTotalPages() {
    event("estimate");
    return 9;
  }
  std::unique_ptr<Page> loadPage(int n) {
    event("load", n, loads);
    return loads++ < loadLimit ? std::make_unique<Page>(n) : nullptr;
  }
  std::optional<uint32_t> getVisibleTextOffsetForPage(int n) {
    event("offset", n);
    return uint32_t(n * 7);
  }
  std::optional<uint16_t> getParagraphIndexForPage(int n) {
    event("paragraph", n);
    return paragraph ? std::optional<uint16_t>(n + 1) : std::nullopt;
  }
};
struct Metadata {
  struct Note {
    std::string href;
  };
  std::vector<Note> notes;
  const std::vector<Note>& footnotes() {
    assert(lockDepth == 1);
    event("notes", notes.size());
    return notes;
  }
};
struct ClippingResult {
  enum class Action : uint8_t { Clip, Lookup, Bookmark };
  Action action = Action::Clip;
  std::string text = "Selected words with enough bytes for an allocation";
  uint16_t startPageOffset = 0, endPageOffset = 1, startWordIndex = 2, endWordIndex = 4, wordCount = 3;
  uint32_t startOffset = 9, endOffset = 18;
};
struct FootnoteResult {
  std::string href = "chapter.xhtml#note";
};
struct ProgressChangeResult {
  int marker = 49;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<std::monostate, ClippingResult, FootnoteResult, ProgressChangeResult> data;
};
struct Child {
  const char* kind;
  explicit Child(const char* k) : kind(k) { std::printf("new-child %s lock=%d\n", kind, lockDepth); }
  virtual ~Child() { std::printf("drop-child %s lock=%d\n", kind, lockDepth); }
};
struct ClipSelectionActivity : Child {
  enum class Mode { Clip, Lookup, DefinitionLookup };
  std::vector<std::unique_ptr<Page>> pages;
  ClipSelectionActivity(GfxRenderer&, MappedInputManager&, std::vector<std::unique_ptr<Page>> p, int l, int t, int x,
                        int y, Mode m)
      : Child("selection"), pages(std::move(p)) {
    event("selection-pos", l, t);
    event("selection-input", x, y);
    event("selection-mode", int(m), pages.size());
  }
};
struct DictionaryWordSelectActivity : Child {
  std::unique_ptr<Page> page;
  DictionaryWordSelectActivity(GfxRenderer&, MappedInputManager&, std::unique_ptr<Page> p, int l, int t,
                               const std::string& s)
      : Child("lookup"), page(std::move(p)) {
    event("lookup-pos", l, t);
    std::printf("lookup-text %s\n", s.c_str());
  }
};
struct EpubReaderFootnoteSelectActivity : Child {
  std::unique_ptr<Page> page;
  EpubReaderFootnoteSelectActivity(GfxRenderer&, MappedInputManager&, std::unique_ptr<Page> p, int l, int t)
      : Child("footnote"), page(std::move(p)) {
    event("footnote-pos", l, t);
  }
};
struct EpubReaderClippingListActivity : Child {
  EpubReaderClippingListActivity(GfxRenderer&, MappedInputManager&) : Child("clippings") {}
};
struct KOReaderSyncActivity : Child {
  KOReaderSyncActivity(GfxRenderer&, MappedInputManager&, const std::string& path, const CrossPointPosition& pos,
                       SavedProgressPosition saved, std::string title, bool events = false)
      : Child("sync") {
    event("sync-pos", pos.marker, events);
    std::printf("sync-data [%s] [%s] [%s]\n", path.c_str(), saved.xpath.c_str(), title.c_str());
  }
};
template <class T, class... A>
std::unique_ptr<T> makeUniqueNoThrow(A&&... a) {
  event("allocation", allocationOk);
  return allocationOk ? std::make_unique<T>(std::forward<A>(a)...) : nullptr;
}
enum class StrId {
  STR_CLIPPING_REMOVED,
  STR_CLIPPING_DELETE_FAILED,
  STR_CLIPPING_LIMIT_REACHED,
  STR_CLIPPING_SAVED,
  STR_CLIPPING_EXPORT_FAILED,
  STR_CLIPPING_FAILED
};
struct ClippingStore {
  enum class AddResult : uint8_t { Added, LimitReached, SaveFailed };
  bool clips = true;
  AddResult result = AddResult::Added;
  bool hasClippings() {
    event("has-clips", clips);
    return clips;
  }
  bool removeClippingAt(size_t i) {
    event("remove", i);
    return removeOk;
  }
  AddResult addClipping(uint16_t spine, uint16_t start, uint16_t end, uint16_t count, uint16_t first, uint16_t last,
                        uint16_t words, const char* chapter, uint16_t paragraph, const std::string& text,
                        uint32_t signature, uint32_t from, uint32_t to) {
    event("add-pos", spine, start);
    event("add-end", end, count);
    event("add-words", first, last);
    event("add-count", words, paragraph);
    event("add-signature", signature);
    event("add-offsets", from, to);
    std::printf("add-text [%s] [%s]\n", chapter, text.c_str());
    return result;
  }
} CLIPPINGS;
struct ClippingsManager {
  static bool saveClipping(const std::string& title, const std::string& author, const std::string& chapter, int page,
                           const std::string& text) {
    event("export", page);
    std::printf("export-text [%s] [%s] [%s] [%s]\n", title.c_str(), author.c_str(), chapter.c_str(), text.c_str());
    return exportOk;
  }
};
struct StripBuffers {
  void releaseIdle() {
    assert(lockDepth == 1);
    event("strips");
  }
};
struct ImageBlock {
  static void releaseRenderCache() { event("images"); }
  static void setExtractor(void*, void*) { event("extractor"); }
};
struct ProgressMapper {
  static SavedProgressPosition toSavedProgress(const std::shared_ptr<Epub>& book, const CrossPointPosition& pos) {
    assert(book && lockDepth == 1);
    event("map", pos.marker);
    return {"/body/DocFragment[2]/body/p[3]/text().7", .25f};
  }
};
struct {
  bool credentials = true;
  bool hasCredentials() {
    event("credentials", credentials);
    return credentials;
  }
} KOREADER_STORE;
struct {
  unsigned getFreeHeap() {
    event("heap");
    return 123456;
  }
} ESP;
struct {
  std::unique_ptr<Child> child;
  void replaceActivity(std::unique_ptr<Child> c) {
    event("replace");
    child = std::move(c);
  }
} activityManager;
constexpr int CLIP_SELECTION_PAGE_WINDOW = 3;
struct EpubReaderActivity {
  struct ModelLock {
    explicit ModelLock(EpubReaderActivity&) {
      assert(!modelDepth);
      ++modelDepth;
    }
    ~ModelLock() { --modelDepth; }
  };
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  StripBuffers stripBuffers;
  Metadata currentPageMetadata;
  int currentSpineIndex = 2, buildViewportWidth = 80, buildViewportHeight = 100, nextPageNumber = 3,
      cachedChapterTotalPageCount = 9, footnoteDepth = 2;
  std::optional<uint32_t> currentPageVisibleOffset;
  bool showDictionaryMessage = false, showBookmarkMessage = false, showClippingMessage = false,
       pendingSyncSaveError = false;
  uint32_t dictionaryMessageTime = 0, bookmarkMessageTime = 0, clippingMessageTime = 0;
  StrId clippingMessage = StrId::STR_CLIPPING_FAILED;
  std::unique_ptr<Child> child;
  std::function<void(const ActivityResult&)> callback;
  template <class T, class F>
  void startActivityForResult(std::unique_ptr<T> c, F f) {
    event("start");
    child = std::move(c);
    callback = std::move(f);
  }
  void requestUpdate() { event("update"); }
  void addBookmark() { event("bookmark"); }
  void openReaderMenu() { event("menu"); }
  void applyProgressChange(const ProgressChangeResult& r) { event("progress", r.marker); }
  void flushReaderSession() { event("flush"); }
  CrossPointPosition getCurrentPosition() {
    event("position");
    return {};
  }
  bool saveProgress(int spine, int page, int count) {
    event("save-progress", spine, page);
    event("save-count", count);
    return saveOk;
  }
  void discardOverlayPage() { event("discard"); }
  void saveLinkStack() { event("links"); }
  void navigateToHref(const std::string& s, bool save) {
    assert(!lockDepth);
    event("navigate", save);
    std::printf("href %s\n", s.c_str());
  }
  int clippingAtPoint(const Page& p, int x, int y) {
    assert(lockDepth == 1);
    event("hit-pos", x, y);
    event("hit-page", p.number, hit);
    return hit;
  }
  void openDictionaryWordSelect();
  void startClipSelection(int x = -1, int y = -1, ClipSelectionActivity::Mode mode = ClipSelectionActivity::Mode::Clip);
  void openFootnoteSelect(bool);
  void openClippings();
  bool launchKOReaderSync();
};
// CHILD_METHODS
int main() {
  for (int operation = 0; operation < 5; ++operation)
    for (int mask = 0; mask < 512; ++mask)
      for (int action = 0; action < 4; ++action) {
        event("case", operation, mask);
        event("action", action);
        loads = 0;
        loadLimit = (mask & 3) == 3 ? 99 : mask & 3;
        allocationOk = !(mask & 4);
        saveOk = !(mask & 8);
        exportOk = !(mask & 16);
        removeOk = !(mask & 32);
        hit = mask & 64 ? 1 : -1;
        toc = mask & 128 ? -1 : 1;
        CLIPPINGS.clips = mask & 256;
        CLIPPINGS.result = static_cast<ClippingStore::AddResult>(action % 3);
        KOREADER_STORE.credentials = mask & 16;
        SETTINGS.dictionaryName[0] = mask & 8 ? '\0' : 'O';
        EpubReaderActivity o;
        o.renderer.hasCache = mask & 64;
        o.footnoteDepth = mask & 32 ? 0 : (mask & 64 ? 1 : 2);
        if (mask & 1) o.currentPageMetadata.notes.push_back({"one.xhtml#note"});
        if (mask & 2) o.currentPageMetadata.notes.push_back({"two.xhtml#note"});
        if ((mask & 48) == 48)
          o.section.reset();
        else {
          o.section->currentPage = mask & 128 ? -1 : 2;
          o.section->paragraph = mask & 64;
        }
        if ((mask & 192) == 192 && operation != 4) o.epub.reset();
        if ((mask & 96) == 96) o.buildViewportWidth = 0;
        switch (operation) {
          case 0:
            o.openDictionaryWordSelect();
            break;
          case 1:
            o.startClipSelection(mask & 32 ? 10 : -1, 20, static_cast<ClipSelectionActivity::Mode>(action % 3));
            break;
          case 2:
            o.openFootnoteSelect(mask & 4);
            break;
          case 3:
            o.openClippings();
            break;
          case 4:
            event("sync-return", o.launchKOReaderSync());
            break;
        }
        if (o.callback) {
          std::string kind = o.child->kind;
          ActivityResult r;
          r.isCancelled = action == 3;
          if (kind == "selection") {
            ClippingResult c;
            c.action = static_cast<ClippingResult::Action>(action % 3);
            c.startPageOffset = mask & 8 ? 65535 : 0;
            c.endPageOffset = mask & 16 ? 65535 : (action == 1 ? 4 : 1);
            r.data = c;
          } else if (kind == "footnote")
            r.data = FootnoteResult{};
          else if (kind == "clippings" && !(mask & 8))
            r.data = ProgressChangeResult{};
          auto callback = std::move(o.callback);
          o.child.reset();
          event("result", r.isCancelled);
          if (mask & 256) o.currentSpineIndex = 7;
          callback(r);
          if (o.callback && o.child && std::string(o.child->kind) == "lookup") {
            auto second = std::move(o.callback);
            o.child.reset();
            event("lookup-result", r.isCancelled);
            second(r);
          }
        }
        event("state-page", o.section ? o.section->currentPage : -99, o.nextPageNumber);
        event("state-spine", o.currentSpineIndex, o.pendingSyncSaveError);
        event("state-messages", o.showDictionaryMessage, o.showBookmarkMessage);
        event("state-clipping", o.showClippingMessage, int(o.clippingMessage));
        event("state-times", o.dictionaryMessageTime, o.bookmarkMessageTime);
        event("state-clip-time", o.clippingMessageTime);
        event("state-offset", o.currentPageVisibleOffset.has_value(), o.currentPageVisibleOffset.value_or(0));
        activityManager.child.reset();
        assert(!lockDepth && !modelDepth);
      }
}
