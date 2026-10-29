#pragma once

#include <LibraryIndexFile.h>
#include <freertos/semphr.h>
#include <xp_ui_home.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/RustActivityAdapter.h"
#include "activities/reader/ReaderChromeListActivity.h"
#include "components/OptionPopup.h"
#include "components/UiAppHelpers.h"

// Index handles and one bounded window of strings stay in the facade; Rust owns
// navigation, grouping state, layout and the published touch geometry.
class LibraryListActivity final : public RustActivityAdapter {
 public:
  LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  ~LibraryListActivity() override { vSemaphoreDelete(resourceMutex); }

  void onEnter() override;
  void onExit() override;

  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  // Order: RenderLock -> ResourceLock -> ModelLock. ResourceLock ends before painting.
  // Only scene construction borrows mutable index/filter data.
  StaticSemaphore_t resourceMutexStorage{};
  SemaphoreHandle_t resourceMutex = xSemaphoreCreateMutexStatic(&resourceMutexStorage);
  class ResourceLock {
    SemaphoreHandle_t mutex;

   public:
    explicit ResourceLock(LibraryListActivity& owner) : mutex(owner.resourceMutex) {
      xSemaphoreTake(mutex, portMAX_DELAY);
    }
    ~ResourceLock() { xSemaphoreGive(mutex); }
    ResourceLock(const ResourceLock&) = delete;
    ResourceLock& operator=(const ResourceLock&) = delete;
  };
  int listCount(const XpUiLibraryState* model = nullptr) const;
  XpUiLibraryState state{};
  bool resourcesChanged = true;  // Reject viewport feedback after a resource transaction.
  Publication publication;
  XpUiHomeListHits publishedHits{};
  freeink::ui::GfxRendererTarget textTarget;
  struct Frame {
    ReaderChromeCommands commands;
    XpUiLibraryState snapshot{};
    XpUiLibraryFeedback next{};
    XpUiHomeListHits hits{};
    XpUiReaderControl headerControls[3]{};
    uint8_t headerCount = 0;
    char position[32]{};
    std::string headerSearchTitle;
  } frame;
  bool nativeLongPress = false, nativeLongTouch = false;
  int nativeTouchX = 0, nativeTouchY = 0;
  uint8_t touchFeedback = 0;
  Publication* rustPublication() override { return &publication; }
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
  bool buildRustScene(XpUiScene&) override;
  void rustPublished() override;
  void rustSceneFailed() override;
  void cancelRejectedInput(const XpUiInput&) override;
  XpUiPaint rustResources() override;
  XpUiHomeRows rows();
  XpUiLibraryFacts facts(const XpUiLibraryState* model = nullptr) const;
  void event(uint8_t, int32_t value = 0, uint16_t count = 0);
  void prepareRows(uint16_t first, uint16_t capacity);
  const char* text(uint32_t) const;
  void paintHeader(const XpUiCommand&);

  // Walk the card and write a fresh index. Blocking, with a popup: at ~70 books
  // it is well under a second, and it only runs when the index is missing or the
  // user asks.
  bool rebuildIndex();

  // Input
  void openSelectedBook();
  void openSearch();
  // Shared tail of row activation and the options menu's Open entry.
  void openBookByPath(const std::string& path);
  void promptRebuildIndex();
  void resetAfterRebuild();
  // Recent-row long-press menu: open / remove from recents / delete / rebuild.
  void showRecentBookOptions(int entry);
  void promptRemoveRecentBook(const std::string& path, const std::string& title);
  // Resolves the row's path and title, then confirms via promptDeleteBookByPath.
  void promptDeleteBook(int entry);
  void promptDeleteBookByPath(const std::string& path, const std::string& title);
  bool collapseGroups(int bookEntry);
  void expandGroup(int groupEntry);
  void restoreExpandedList();
  // Sub-screens act on button press, so a button still held when we resume must
  // not also act here. Records what to swallow on the next release.
  void swallowHeldReleases();

  // Data
  void applyFilter();
  int bookRowCount(const XpUiLibraryState* model = nullptr) const;
  int rowFor(int entry, const XpUiLibraryState* model = nullptr) const;
  // fileName, when asked for, is the on-card name the row's icon derives from
  // (the display title may come from metadata and carry no extension).
  bool rowTextFor(int entry, std::string& title, std::string& author, std::string* fileName = nullptr,
                  const XpUiLibraryState* model = nullptr);
  uint32_t titleInitialFor(int entry, const XpUiLibraryState* model = nullptr);
  bool buildGroupStarts();
  int groupForBook(int bookEntry) const;
  bool groupable() const;

  static void formatInitialHeading(uint32_t initial, std::string& out);
  void formatAuthorHeading(const std::string& author, std::string& out) const;
  const char* headerTitle() const;

  // Ring 0 is the strip; the selected BOOK is ring - 1, with the strip keeping
  // row 0 as the working selection exactly as the pre-ring code did.
  int selectedEntry() const;

  // --- pinned recently-opened overlay ---------------------------------------
  // On the unfiltered Recent shelf the RecentBooksStore entries sit on top, in
  // read order; the modification-time list follows with those books skipped.
  // Entries below pinnedCount() are store rows; the rest go through rowFor().
  int pinnedCount(const XpUiLibraryState* model = nullptr) const;
  // Re-match the store against the index (chunked scan). Call whenever the
  // index or the store changes.
  void resolvePinned();
  // Direction-space translation of the matched rows. Call on sort toggles.
  void refreshOverlap();

  library::LibraryIndexFile index;

  // Rows surviving the current query, as positions in the active sort order.
  // Empty query means no filtering and this owns no allocation, so the ordinary
  // shelf pays nothing proportional to the library for the feature.
  std::string query;
  std::unique_ptr<uint16_t[]> filtered;
  uint16_t filteredCount = 0;
  bool filterFailed = false;

  // One start row per group. Grouping is only offered for the sorted <=512-book
  // index, so this fallible allocation is at most 1 KiB and is reused after its
  // first successful allocation.
  std::unique_ptr<uint16_t[]> groupStarts;
  uint16_t groupCapacity = 0;
  uint16_t groupCount = 0;

  // Visible-window row storage, reused across renders. Bounded by
  // the densest page, never by the library. Headings get their own storage:
  // the surname-first inversion must not overwrite the author slot, whose raw
  // value the next row's group comparison reads.
  struct WindowRow {
    freeink::ui::BitmapRef icon;
    int16_t heading = -1;
    bool subtitle = false;
  };
  std::vector<WindowRow> winItems;
  uint16_t windowStart = 0;
  std::vector<std::string> winTitles;
  std::vector<std::string> winAuthors;
  std::vector<std::string> winHeaders;

  // Pinned overlay state: per store entry its RecentAsc row (0xFFFF when the
  // book is not in the index), and the current-direction rows to skip, sorted
  // ascending, so unpinned entries map to sort rows with a <=10-step walk.
  uint16_t pinnedAscRows[RecentBooksStore::MAX_RECENT_BOOKS] = {};
  uint16_t overlapRows[RecentBooksStore::MAX_RECENT_BOOKS] = {};
  uint8_t pinnedTotal = 0;
  uint8_t overlapCount = 0;

  // Row options modal (Recent long-press menu); owned here so it outlives the
  // touch event that opened it.
  OptionPopup optionPopup;
};
