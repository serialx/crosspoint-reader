#include "LibraryListActivity.h"

#include <BoardConfig.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryText.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/headerIcons.h"
#include "components/icons/listIcons.h"
#include "components/icons/search32.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr int SIDE_PADDING = 12;

// Side clearance for the footer text in the panel's bottom corners.
int bottomCornerClearance() {
  const auto& board = BoardConfig::ACTIVE;
  return board.bottomCornerRadius != 0 ? board.bottomCornerRadius : board.cornerRadius;
}

constexpr unsigned long LONG_PRESS_MS = 1000;

const char* tabLabelFor(const int tab) {
  if (tab == 1) return tr(STR_LIBRARY_TAB_TITLE);
  if (tab == 2) return tr(STR_LIBRARY_TAB_AUTHOR);
  return tr(STR_LIBRARY_TAB_RECENT);
}

}  // namespace

LibraryListActivity::LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : RustActivityAdapter("Library", renderer, mappedInput), textTarget(makeUiTarget(renderer)) {}

void LibraryListActivity::onEnter() {
  // One lock across the base lifecycle AND the data phase: the base onEnter
  // schedules a paint, and the render task must not read the index or the
  // filter before they are in place. The rebuild also needs the lock: the
  // render task's SD-loaded fonts read glyph data at draw time, and the walk
  // needs the card to itself.
  RenderLock lock(*this);
  Activity::onEnter();
  xp_ui_library_init(&state);
  refreshSharedUiThemeTokens(textTarget);
  requestUpdate();

  // Recent is backed by the resident store. Prune before opening the index so
  // its persistence write never overlaps the long-lived index reader.
  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();

  // Rebuild when the index is missing, invalid, or was built with the other
  // metadata mode. Otherwise entering the screen stays instant.
  const bool readMetadata = SETTINGS.libraryUseMetadata != 0;
  const bool rebuildNeeded = library::isLibraryIndexDirty() || !index.open(library::libraryIndexPath()) ||
                             index.header().metadataEnabled != readMetadata;
  if (rebuildNeeded) {
    index.close();
    GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
    rebuildIndex();
    if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot open library index");
  }
  event(XP_UI_LIBRARY_DEGRADED, index.isOpen() && index.ranksDegraded());
  if (index.isOpen() && index.dedupDegraded()) {
    LOG_ERR("LIB", "index was built without duplicate detection");
  }
  resolvePinned();

  // Entered while Confirm was still held (typical when launched from the home
  // menu): ignore its release, or we would open whatever sits at row 0.
  event(XP_UI_LIBRARY_SUPPRESS, mappedInput.isPressed(MappedInputManager::Button::Confirm));
  requestUpdate(true);
}

void LibraryListActivity::onExit() {
  index.close();
  Activity::onExit();
}

bool LibraryListActivity::rebuildIndex() {
  library::BuildStats stats;
  const bool ok = library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0);
  if (!ok) {
    LOG_ERR("LIB", "index build failed");
    return false;
  }
  LOG_INF("LIB", "reconciled: %u unchanged, %u added, %u renamed, %u removed, %u enriched (%u dup, %u unreadable)",
          static_cast<unsigned>(stats.unchanged), static_cast<unsigned>(stats.added),
          static_cast<unsigned>(stats.renamed), static_cast<unsigned>(stats.removed),
          static_cast<unsigned>(stats.enriched), static_cast<unsigned>(stats.duplicatesDropped),
          static_cast<unsigned>(stats.unreadableSkipped));
  if (stats.dedupDegraded) LOG_ERR("LIB", "rebuild completed without duplicate detection");
  return true;
}

void LibraryListActivity::swallowHeldReleases() {
  event(XP_UI_LIBRARY_SUPPRESS, mappedInput.isPressed(MappedInputManager::Button::Confirm) |
                                    (mappedInput.isPressed(MappedInputManager::Button::Back) << 1));
}

int LibraryListActivity::selectedEntry() const { return xp_ui_library_entry(&state); }

// The pinned overlay applies only to the shelf that reads as "what am I up
// to": the unfiltered Recent sort, newest first. A search result is a flat
// list the reader narrowed down on purpose, and the ascending toggle asks for
// oldest-first, which pinned fresh reads would contradict.
int LibraryListActivity::pinnedCount(const XpUiLibraryState* model) const {
  return xp_ui_library_pinned(model ? model : &state, pinnedTotal);
}

void LibraryListActivity::resolvePinned() {
  const auto& books = RECENT_BOOKS.getBooks();
  pinnedTotal = static_cast<uint8_t>(std::min<size_t>(books.size(), RecentBooksStore::MAX_RECENT_BOOKS));
  for (int i = 0; i < pinnedTotal; i++) pinnedAscRows[i] = 0xFFFF;
  if (pinnedTotal > 0 && index.isOpen()) {
    library::BookIdentity identities[RecentBooksStore::MAX_RECENT_BOOKS];
    for (int i = 0; i < pinnedTotal; i++) {
      const std::string& path = books[static_cast<size_t>(i)].path;
      identities[i].pathHash = library::clixPathHash(path.data(), path.size());
      // Size is only a lookup prefilter; 0 (stat failed, e.g. the index handle
      // is the card's one open reader) falls back to hash-only matching.
      identities[i].fileSize = 0;
      HalFile file;
      if (Storage.openFileForRead("LIB", path.c_str(), file)) {
        identities[i].fileSize = static_cast<uint32_t>(file.fileSize());
      }
    }
    if (!index.recentRowsFor(identities, pinnedTotal, pinnedAscRows)) {
      // Without the match the overlay would duplicate every pinned book that is
      // also in the index; better to drop the pins than to show doubles.
      LOG_ERR("LIB", "recent-book lookup failed; overlay disabled");
      pinnedTotal = 0;
    }
  }
  refreshOverlap();
}

void LibraryListActivity::refreshOverlap() {
  overlapCount = xp_ui_library_overlap(pinnedAscRows, pinnedTotal, index.bookCount(),
                                       xp_ui_library_flags(&state, 0) & 32, overlapRows);
}

void LibraryListActivity::openSelectedBook() {
  std::string path;
  if (selectedEntry() < pinnedCount()) {
    const auto& books = RECENT_BOOKS.getBooks();
    if (selectedEntry() >= static_cast<int>(books.size())) return;
    path = books[static_cast<size_t>(selectedEntry())].path;
  } else {
    if (!index.isOpen()) return;
    const uint16_t ordinal = index.ordinalForRow(static_cast<library::SortOrder>(state.sort),
                                                 static_cast<uint16_t>(rowFor(selectedEntry())));
    if (ordinal == 0xFFFF) return;

    library::ClixRecord record{};
    if (!index.readRecord(ordinal, record) || !index.readPath(record, path)) {
      LOG_ERR("LIB", "cannot resolve path for row %d", selectedEntry());
      return;
    }
  }
  openBookByPath(path);
}

// Release the index reader before the book opens its own files.
void LibraryListActivity::openBookByPath(const std::string& path) {
  index.close();
  onSelectBook(path);
}

// Recent-shelf long-press menu (button hold and touch long-press). The first
// rows may come from RecentBooksStore; the rest are index rows sorted by
// modification time. Only store rows can be removed from recents.
void LibraryListActivity::showRecentBookOptions(const int entry) {
  if (entry < 0 || entry >= listCount()) return;

  std::string path;
  std::string title;
  const bool isStoreRow = entry < pinnedCount();
  if (isStoreRow) {
    const auto& books = RECENT_BOOKS.getBooks();
    if (entry >= static_cast<int>(books.size())) return;
    path = books[static_cast<size_t>(entry)].path;
    title = books[static_cast<size_t>(entry)].title;
  } else {
    if (!index.isOpen()) return;
    const uint16_t ordinal =
        index.ordinalForRow(static_cast<library::SortOrder>(state.sort), static_cast<uint16_t>(rowFor(entry)));
    library::ClixRecord record{};
    std::string author;
    if (ordinal == 0xFFFF || !index.readRecord(ordinal, record) || !index.readPath(record, path) ||
        !rowTextFor(entry, title, author)) {
      LOG_ERR("LIB", "cannot resolve Recent row %d", entry);
      return;
    }
  }

  const char* STORE_OPTIONS[] = {tr(STR_OPEN), tr(STR_REMOVE_FROM_RECENTS), tr(STR_DELETE), tr(STR_LIBRARY_REBUILD)};
  const char* INDEX_OPTIONS[] = {tr(STR_OPEN), tr(STR_DELETE), tr(STR_LIBRARY_REBUILD)};
  optionPopup.show(tr(STR_LIBRARY), title.c_str(), isStoreRow ? STORE_OPTIONS : INDEX_OPTIONS, isStoreRow ? 4 : 3, 0,
                   [this, path, title, isStoreRow](const int choice) {
                     swallowHeldReleases();
                     switch (choice) {
                       case 0:
                         openBookByPath(path);
                         break;
                       case 1:
                         if (isStoreRow) {
                           promptRemoveRecentBook(path, title);
                         } else {
                           promptDeleteBookByPath(path, title);
                         }
                         break;
                       case 2:
                         if (isStoreRow)
                           promptDeleteBookByPath(path, title);
                         else
                           promptRebuildIndex();
                         break;
                       case 3:
                         if (isStoreRow) promptRebuildIndex();
                         break;
                       default:
                         break;
                     }
                   });
  requestUpdate();
}

// Manual index refresh, same card discipline as the onEnter rebuild: the walk
// wants the card to itself, and the render task must not read the index (or
// the filter) around it.
void LibraryListActivity::promptRebuildIndex() {
  RenderLock lock(*this);
  GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
  index.close();
  rebuildIndex();
  if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot open library index");
  resetAfterRebuild();
  requestUpdate(true);
}

void LibraryListActivity::resetAfterRebuild() {
  applyFilter();
  resolvePinned();
  event(XP_UI_LIBRARY_REBUILT, 0, listCount());
}

void LibraryListActivity::promptRemoveRecentBook(const std::string& path, const std::string& title) {
  const bool reopenIndex = index.isOpen();
  index.close();
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title);
  if (!confirmation) {
    LOG_ERR("LIB", "OOM: recent removal confirmation");
    if (reopenIndex && !index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    return;
  }

  startActivityForResult(std::move(confirmation), [this, path, reopenIndex](const ActivityResult& result) {
    swallowHeldReleases();
    RenderLock lock(*this);
    if (reopenIndex && !index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    if (!result.isCancelled && RECENT_BOOKS.removeByPath(path)) {
      resolvePinned();
      event(XP_UI_LIBRARY_CLOSE_REVEAL);
      event(XP_UI_LIBRARY_REBUILT, 0, listCount());
    }
  });
}

void LibraryListActivity::promptDeleteBook(const int entry) {
  if (!index.isOpen() || entry < 0 || entry >= bookRowCount()) return;
  const uint16_t ordinal =
      index.ordinalForRow(static_cast<library::SortOrder>(state.sort), static_cast<uint16_t>(rowFor(entry)));
  if (ordinal == 0xFFFF) return;

  std::string path;
  library::ClixRecord record{};
  if (!index.readRecord(ordinal, record) || !index.readPath(record, path)) {
    LOG_ERR("LIB", "cannot resolve path for row %d", entry);
    return;
  }
  std::string title;
  std::string author;
  rowTextFor(entry, title, author);
  promptDeleteBookByPath(path, title);
}

void LibraryListActivity::promptDeleteBookByPath(const std::string& path, const std::string& title) {
  // The dialog and the delete both want the card; reopen when we resume.
  index.close();
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ITEM_CONFIRM), title);
  if (!confirmation) {
    LOG_ERR("LIB", "OOM: delete confirmation");
    if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    return;
  }

  startActivityForResult(std::move(confirmation), [this, path](const ActivityResult& result) {
    swallowHeldReleases();
    {
      // Same lock rationale as onEnter: the walk wants the card to itself, and
      // the render task must not read the index (or the filter) around the
      // rebuild.
      RenderLock lock(*this);
      if (!result.isCancelled) {
        LOG_DBG("LIB", "deleting %s", path.c_str());
        if (!removeBookFile(path)) LOG_ERR("LIB", "cannot delete %s", path.c_str());
        if (RECENT_BOOKS.removeByPath(path)) RECENT_BOOKS.saveToFile();
        GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
        rebuildIndex();
      }
      if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
      if (!result.isCancelled) {
        resetAfterRebuild();
      }
    }
    if (!result.isCancelled) {
      event(XP_UI_LIBRARY_CLOSE_REVEAL);
      requestUpdate(true);
    }
  });
}

void LibraryListActivity::openSearch() {
  // No key filtering here on purpose. Greying out the letters that lead nowhere
  // was built, tested on device and removed: a letter you can see but cannot
  // reach reads as a broken keyboard, and the eye keeps returning to it.
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_LIBRARY_SEARCH), query, 48,
                                                           InputType::Text);
  if (!keyboard) {
    LOG_ERR("LIB", "OOM: search keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    swallowHeldReleases();
    if (result.isCancelled) return;
    ResourceLock lock(*this);
    query = std::get<KeyboardResult>(result.data).text;
    applyFilter();
    event(XP_UI_LIBRARY_QUERY_LOADED, 0, filteredCount);
    requestUpdate();
  });
}

int LibraryListActivity::bookRowCount(const XpUiLibraryState* model) const {
  return xp_ui_library_books(model ? model : &state, index.bookCount(), filteredCount, pinnedTotal, overlapCount);
}

int LibraryListActivity::listCount(const XpUiLibraryState* model) const {
  return (model ? model : &state)->collapsed ? groupCount : bookRowCount(model);
}

// Entry position on screen to row position in the sort order. Identity while
// unfiltered and unpinned, so the shelf costs nothing when nothing is typed.
// With pins active, entries below pinnedCount() belong to the store and must
// not reach this; the rest walk past the pinned books' own sort rows.
int LibraryListActivity::rowFor(int entry, const XpUiLibraryState* model) const {
  return xp_ui_library_row(model ? model : &state, entry, pinnedTotal, filtered.get(), filteredCount, overlapRows,
                           overlapCount);
}

bool LibraryListActivity::groupable() const { return xp_ui_library_flags(&state, bookRowCount()) & 2; }

uint32_t LibraryListActivity::titleInitialFor(const int entry, const XpUiLibraryState* model) {
  const uint16_t ordinal = index.ordinalForRow(static_cast<library::SortOrder>((model ? model : &state)->sort),
                                               static_cast<uint16_t>(rowFor(entry, model)));
  library::ClixRecord record{};
  if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) return 0;
  return library::foldedGroupInitial(std::string_view(record.fold, record.foldLen));
}

bool LibraryListActivity::buildGroupStarts() {
  const int count = bookRowCount();
  if (count <= 0) return false;
  if (groupCapacity < count) {
    auto starts = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(count));
    if (!starts) {
      LOG_ERR("LIB", "cannot allocate %u-byte group map", static_cast<unsigned>(count * sizeof(uint16_t)));
      return false;
    }
    groupStarts = std::move(starts);
    groupCapacity = static_cast<uint16_t>(count);
  }

  groupCount = 0;
  uint32_t previousInitial = 0;
  std::string previousAuthor;
  std::string title;
  std::string author;
  previousAuthor.reserve(128);
  title.reserve(128);
  author.reserve(128);
  for (int entry = 0; entry < count; entry++) {
    bool startsGroup = entry == 0;
    if ((xp_ui_library_flags(&state, 0) & 16)) {
      rowTextFor(entry, title, author);
      startsGroup = startsGroup || author != previousAuthor;
      previousAuthor = author;
    } else {
      const uint32_t initial = titleInitialFor(entry);
      startsGroup = startsGroup || initial != previousInitial;
      previousInitial = initial;
    }
    if (startsGroup) groupStarts[groupCount++] = static_cast<uint16_t>(entry);
  }
  LOG_DBG("LIB", "group map: %u groups, %u bytes", static_cast<unsigned>(groupCount),
          static_cast<unsigned>(groupCapacity * sizeof(uint16_t)));
  return groupCount > 0;
}

int LibraryListActivity::groupForBook(int entry) const {
  return xp_ui_library_group(entry, groupStarts.get(), groupCount);
}

bool LibraryListActivity::collapseGroups(int entry) {
  ResourceLock lock(*this);
  if (!groupable() || !buildGroupStarts()) return false;
  event(XP_UI_LIBRARY_COLLAPSED, groupForBook(entry));
  requestUpdate();
  return true;
}

void LibraryListActivity::expandGroup(int entry) {
  ResourceLock lock(*this);
  if (!state.collapsed || entry < 0 || entry >= groupCount) return;
  event(XP_UI_LIBRARY_EXPANDED, groupStarts[entry]);
  requestUpdate();
}

void LibraryListActivity::restoreExpandedList() {
  event(XP_UI_LIBRARY_EXPANDED, -1);
  requestUpdate();
}

// One pass over the sort order, keeping what matches. No index, no cache: at the
// 4096-book format cap this is 4096 comparisons of at most 96 bytes. The result
// array is allocated once with the exact upper bound and fails back to an
// explicit message rather than letting vector growth abort the firmware.
void LibraryListActivity::applyFilter() {
  event(XP_UI_LIBRARY_FILTER_STARTED, !query.empty());
  groupCount = 0;
  filtered.reset();
  filteredCount = 0;
  filterFailed = false;
  if (query.empty()) return;

  const std::string needle = library::fold(query);
  const int total = static_cast<int>(index.bookCount());
  if (total <= 0) return;

  auto matches = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(total));
  if (!matches) {
    LOG_ERR("LIB", "cannot allocate %u-byte search result buffer", static_cast<unsigned>(total * sizeof(uint16_t)));
    filterFailed = true;
    return;
  }

  uint16_t matchCount = 0;
  std::string author;
  for (int row = 0; row < total; row++) {
    const uint16_t ordinal =
        index.ordinalForRow(static_cast<library::SortOrder>(state.sort), static_cast<uint16_t>(row));
    library::ClixRecord record{};
    if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) continue;
    if (library::matchesQuery(std::string_view(record.fold, record.foldLen), needle)) {
      matches[matchCount++] = static_cast<uint16_t>(row);
      continue;
    }
    // The stored fold covers the title only, so the author has to be read and
    // folded here. That is the search most worth having: the reader who knows
    // the author usually also knows where the book is, while "emily" finding
    // Alice Hunter is the case the shelf exists to answer.
    author.clear();
    if (index.readAuthor(record, author) && library::matchesQuery(library::fold(author), needle)) {
      matches[matchCount++] = static_cast<uint16_t>(row);
    }
  }
  filtered = std::move(matches);
  filteredCount = matchCount;
}

// Title and author for one entry, read straight from the index. Only ever
// called for rows about to be drawn, so at most a screenful of strings exists
// at once.
bool LibraryListActivity::rowTextFor(const int entry, std::string& title, std::string& author, std::string* fileName,
                                     const XpUiLibraryState* model) {
  title.clear();
  author.clear();
  if (fileName) fileName->clear();
  if (entry < pinnedCount(model)) {
    const auto& books = RECENT_BOOKS.getBooks();
    if (entry < 0 || entry >= static_cast<int>(books.size())) return false;
    const auto& book = books[static_cast<size_t>(entry)];
    title = book.title;
    author = book.author;
    if (fileName) *fileName = book.path;
    return true;
  }
  const uint16_t ordinal = index.ordinalForRow(static_cast<library::SortOrder>((model ? model : &state)->sort),
                                               static_cast<uint16_t>(rowFor(entry, model)));
  library::ClixRecord record{};
  if (ordinal != 0xFFFF && index.readRecord(ordinal, record)) {
    // The build already decided both fields — from the book's own metadata when
    // it has any, and with one spelling chosen per author across the library.
    // Re-parsing the name here would throw that away, and only works while the
    // name still looks like "Title - Author".
    if (!index.readAuthor(record, author)) author.clear();
    // The stored title when the book gave one, the filename otherwise.
    if (!index.readTitle(record, title) || title.empty()) index.readName(record, title);
    if (fileName) index.readName(record, *fileName);
  }
  if (title.empty()) title = tr(STR_LIBRARY_UNKNOWN_TITLE);
  return true;
}

void LibraryListActivity::formatInitialHeading(uint32_t initial, std::string& out) {
  out.clear();
  if (initial == 0) {
    out.push_back('#');
    return;
  }
  if (initial >= 'a' && initial <= 'z') initial -= 'a' - 'A';
  utf8AppendCodepoint(initial, out);
}

void LibraryListActivity::formatAuthorHeading(const std::string& author, std::string& out) const {
  out = author.empty() ? std::string(tr(STR_LIBRARY_UNKNOWN_AUTHOR)) : author;
  if (author.empty()) return;
  const size_t lastSpace = out.find_last_of(' ');
  if (lastSpace != std::string::npos && lastSpace + 1 < out.size()) {
    out = out.substr(lastSpace + 1) + ", " + out.substr(0, lastSpace);
  }
}

const char* LibraryListActivity::headerTitle() const {
  if (!frame.headerSearchTitle.empty()) return frame.headerSearchTitle.c_str();
  return frame.snapshot.degraded ? tr(STR_LIBRARY_TITLE_UNSORTED) : tr(STR_LIBRARY);
}

// OptionPopup is a self-contained modal: it owns the whole frame (hints
// included) whenever it is up, mirroring the FileBrowser pattern.
void LibraryListActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  RustActivityAdapter::render(std::move(lock));
}

void LibraryListActivity::event(uint8_t code, int32_t value, uint16_t count) {
  ModelLock lock(*this);
  xp_ui_library_event(&state, code, value, count);
  resourcesChanged = true;
}

XpUiLibraryFacts LibraryListActivity::facts(const XpUiLibraryState* model) const {
  return {static_cast<uint16_t>(listCount(model)),
          static_cast<uint16_t>(bookRowCount(model)),
          nativeLongPress,
          nativeLongTouch,
          mappedInput.hasTouch(),
          filterFailed};
}

void LibraryListActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  if (consumeRejectedInput()) return;
  mappedInput.wasSwipe(&nativeTouchX, &nativeTouchY);
  const bool longPress = mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, LONG_PRESS_MS);
  bool ready;
  {
    ModelLock lock(*this);
    ready = publication.ready;
  }
  const bool longTouch = ready && mappedInput.wasScreenLongPress(nativeTouchX, nativeTouchY);
  {
    ModelLock lock(*this);
    nativeLongPress = longPress;
    nativeLongTouch = longTouch;
  }
  RustActivityAdapter::loop();
}

XpUiInterest LibraryListActivity::rustInterest(uint32_t now) const {
  auto interest = xp_ui_library_interest(&state, now);
  if (nativeLongPress || nativeLongTouch) {
    interest.held = 0;
    interest.deadline_enabled = 1;
    interest.deadline_ms = now;
  }
  return interest;
}

XpUiEffect LibraryListActivity::updateRust(const XpUiInput& source) {
  auto input = source;
  if (publication.ready && (input.touch_flags || nativeLongTouch || input.swipe == XP_UI_SWIPE_LEFT)) {
    XpUiInteraction hit{};
    const bool alternate = nativeLongTouch || input.swipe == XP_UI_SWIPE_LEFT;
    if (xp_ui_home_list_hit(&publishedHits, alternate ? nativeTouchX : input.touch_x,
                            alternate ? nativeTouchY : input.touch_y, &hit)) {
      input.action = hit.action;
      input.value = hit.value;
    } else
      nativeLongTouch = false;
  }
  const auto f = facts();
  const auto result = xp_ui_library_update(&state, &input, &f);
  touchFeedback = result.haptic;
  return result.effect;
}

void LibraryListActivity::applyRustEffect(const XpUiEffect& effect) {
  bool longTouch;
  uint8_t feedback;
  {
    ModelLock lock(*this);
    longTouch = nativeLongTouch;
    feedback = touchFeedback;
    touchFeedback = 0;
  }
  if (feedback) haptic_feedback::touchAction(longTouch);
  switch (effect.kind) {
    case XP_UI_SCROLL:
      LOG_DBG("LIST", "%s swipe delta=%d count=%d", name.c_str(), effect.value, listCount());
      break;
    case XP_UI_ACTIVATE:
      openSelectedBook();
      break;
    case XP_UI_LIBRARY_HOME:
      onGoHome();
      break;
    case XP_UI_LIBRARY_SEARCH:
      openSearch();
      break;
    case XP_UI_LIBRARY_OPTIONS:
      showRecentBookOptions(effect.value);
      break;
    case XP_UI_LIBRARY_DELETE:
      if ((xp_ui_library_flags(&state, 0) & 8) && effect.value < pinnedCount()) {
        const auto& book = RECENT_BOOKS.getBooks()[static_cast<size_t>(effect.value)];
        promptDeleteBookByPath(book.path, book.title);
      } else
        promptDeleteBook(effect.value);
      break;
    case XP_UI_LIBRARY_COLLAPSE:
      collapseGroups(effect.value);
      break;
    case XP_UI_LIBRARY_EXPAND:
      restoreExpandedList();
      break;
    case XP_UI_LIBRARY_EXPAND_SELECTED:
      expandGroup(effect.value);
      break;
    case XP_UI_LIBRARY_CLEAR_QUERY: {
      ResourceLock lock(*this);
      query.clear();
      applyFilter();
      event(XP_UI_LIBRARY_QUERY_CLEARED);
      requestUpdate();
      break;
    }
    case XP_UI_LIBRARY_SORT: {
      ResourceLock lock(*this);
      event(XP_UI_LIBRARY_SORT_BEGIN, effect.value);
      applyFilter();
      event(XP_UI_LIBRARY_SORT_FINISH, effect.value);
      refreshOverlap();
      requestUpdate();
      break;
    }
    case XP_UI_LIBRARY_REBUILD:
      promptRebuildIndex();
      break;
  }
}

void LibraryListActivity::prepareRows(uint16_t first, uint16_t capacity) {
  const auto flags = xp_ui_library_flags(&frame.snapshot, bookRowCount(&frame.snapshot));
  const bool authorGrouped = flags & 16;
  const bool grouped = !(flags & 8);
  const bool collapsed = frame.snapshot.collapsed;
  if (winTitles.size() < capacity) winTitles.resize(capacity);
  if (winAuthors.size() < capacity) winAuthors.resize(capacity);
  if (!collapsed && winHeaders.size() < capacity) winHeaders.resize(capacity);
  winItems.clear();
  if (winItems.capacity() < capacity) winItems.reserve(capacity);
  windowStart = first;
  int headers = 0;
  uint32_t previousInitial = 0;
  std::string rowFile;
  rowFile.reserve(128);
  for (int entry = first; entry < listCount(&frame.snapshot) && winItems.size() < capacity; ++entry) {
    const size_t row = winItems.size();
    auto& title = winTitles[row];
    auto& author = winAuthors[row];
    WindowRow item;
    if (collapsed) {
      const int book = groupStarts[entry];
      if (authorGrouped) {
        rowTextFor(book, title, author, nullptr, &frame.snapshot);
        formatAuthorHeading(author, title);
      } else
        formatInitialHeading(titleInitialFor(book, &frame.snapshot), title);
    } else {
      if (!rowTextFor(entry, title, author, &rowFile, &frame.snapshot)) continue;
      uint32_t initial = 0;
      bool starts = false;
      if (authorGrouped)
        starts = row == 0 || author != winAuthors[row - 1];
      else if (grouped) {
        initial = titleInitialFor(entry, &frame.snapshot);
        starts = row == 0 || initial != previousInitial;
        previousInitial = initial;
      }
      if (starts) {
        item.heading = headers;
        auto& heading = winHeaders[headers++];
        if (authorGrouped)
          formatAuthorHeading(author, heading);
        else
          formatInitialHeading(initial, heading);
      }
      item.subtitle = !authorGrouped && !author.empty();
      if (!rowFile.empty()) item.icon = listIconFor(UITheme::getFileIcon(rowFile), 32);
    }
    winItems.push_back(item);
  }
}

const char* LibraryListActivity::text(uint32_t h) const {
  if (h > 0 && h < 0x10000000u) {
    const size_t index = (h - 1) / 4;
    if (index >= winItems.size()) return nullptr;
    switch ((h - 1) % 4) {
      case 0:
        return winTitles[index].c_str();
      case 1:
        return winAuthors[index].c_str();
      case 2:
        return winItems[index].heading < 0 ? nullptr : winHeaders[winItems[index].heading].c_str();
      default:
        return nullptr;
    }
  }
  if (h >= XP_UI_LIBRARY_TAB && h < XP_UI_LIBRARY_TAB + 3) return tabLabelFor(h - XP_UI_LIBRARY_TAB);
  switch (h) {
    case XP_UI_LIBRARY_POSITION:
      return frame.position;
    case XP_UI_LIBRARY_EMPTY:
      return tr(STR_LIBRARY_EMPTY);
    case XP_UI_LIBRARY_NO_RESULTS:
      return tr(STR_LIBRARY_NO_RESULTS);
    case XP_UI_LIBRARY_UNAVAILABLE:
      return tr(STR_LIBRARY_SEARCH_UNAVAILABLE);
    case XP_UI_LIBRARY_HELP + 1:
      return tr(STR_LIBRARY_HOLD_SORT);
    case XP_UI_LIBRARY_HELP + 2:
      return tr(STR_LIBRARY_HOLD_OPTIONS);
    case XP_UI_LIBRARY_HELP + 3:
      return tr(STR_HOLD_OPEN_TO_DELETE);
    case XP_UI_LIBRARY_HELP + 4:
      return tr(STR_LIBRARY_HOLD_GROUPS);
    default:
      return nullptr;
  }
}

XpUiHomeRows LibraryListActivity::rows() {
  return {this,
          [](void* p, uint16_t first, uint16_t count) -> uint8_t {
            static_cast<LibraryListActivity*>(p)->prepareRows(first, count);
            return true;
          },
          [](void* p, uint16_t entry, XpUiHomeRow* out) -> uint8_t {
            auto& self = *static_cast<LibraryListActivity*>(p);
            if (entry < self.windowStart || entry - self.windowStart >= self.winItems.size()) return false;
            const size_t index = entry - self.windowStart;
            const auto& item = self.winItems[index];
            const uint32_t h = index * 4 + 1;
            *out = {h,
                    item.subtitle ? h + 1 : 0,
                    0,
                    item.icon ? h + 3 : 0,
                    item.heading >= 0 ? h + 2 : 0,
                    static_cast<int16_t>(item.icon.width),
                    0};
            return true;
          },
          [](void* p, uint32_t h, int32_t f, uint8_t b) -> int32_t {
            auto& self = *static_cast<LibraryListActivity*>(p);
            const char* s = self.text(h);
            return s ? self.renderer.getTextWidth(f, s, static_cast<EpdFontFamily::Style>(b)) : -1;
          },
          [](void* p, uint32_t h, int32_t f, uint8_t b, uint8_t lines, int16_t width) -> int32_t {
            auto& self = *static_cast<LibraryListActivity*>(p);
            const char* s = self.text(h);
            if (!s) return -1;
            fui::GfxRendererTarget target(self.renderer);
            target.setFont(0, f);
            fui::TextStyle style;
            style.bold = b;
            style.maxLines = lines;
            return fui::measureWrappedText(target, s, style, width).height;
          },
          [](void* p, int32_t f) -> int32_t { return static_cast<LibraryListActivity*>(p)->renderer.getLineHeight(f); },
          nullptr,
          0,
          [](void* p, int32_t selected, uint16_t count, uint8_t groups) -> uint8_t {
            auto& self = *static_cast<LibraryListActivity*>(p);
            snprintf(self.frame.position, sizeof(self.frame.position),
                     groups ? tr(STR_LIBRARY_GROUP_POSITION) : tr(STR_LIBRARY_POSITION), selected, count);
            return true;
          }};
}

bool LibraryListActivity::buildRustScene(XpUiScene& scene) {
  ResourceLock resourcesLock(*this);
  frame.headerSearchTitle.clear();
  if (!query.empty()) frame.headerSearchTitle.append("“").append(query).append("”");
  {
    ModelLock lock(*this);
    frame.snapshot = state;
    resourcesChanged = false;
  }
  XpUiLibraryLayout layout{};
  sampleReaderChromeTheme(renderer, layout.theme);
  layout.theme.help_font = SMALL_FONT_ID;
  const auto& m = UITheme::getInstance().getMetrics();
  const auto& tokens = *sharedUiThemeCell().load(std::memory_order_acquire);
  const auto safe = textTarget.deviceContext().safeRect();
  const auto content = UITheme::getContentArea(renderer);
  layout.tabs = {layout.theme.screen,
                 {safe.x, safe.y, safe.width, safe.height},
                 layout.theme.body_font,
                 layout.theme.small_font,
                 static_cast<int16_t>(m.tabBarHeight),
                 static_cast<int16_t>(m.tabSpacing),
                 static_cast<int16_t>(m.contentSidePadding),
                 static_cast<int16_t>(std::max(1, m.headerUnderlineSize)),
                 static_cast<int16_t>(m.verticalSpacing),
                 m.tabPillFullSlot,
                 mappedInput.hasTouch(),
                 tokens.listRowRadius,
                 tokens.bodyText.bold,
                 {0, 0}};
  layout.content = {static_cast<int16_t>(content.x), static_cast<int16_t>(content.y),
                    static_cast<int16_t>(content.width), static_cast<int16_t>(content.height)};
  layout.hints_height = m.buttonHintsHeight;
  layout.corner = bottomCornerClearance();
  const auto resources = rows();
  const auto f = facts(&frame.snapshot);
  frame.headerCount = 0;
  if (!frame.commands.ready() && !frame.commands.reserve(48)) return false;
  for (;;) {
    const auto buffer = frame.commands.buffer();
    const auto status =
        xp_ui_library_view(&frame.snapshot, &f, &layout, &resources, &buffer, &frame.next, &frame.hits, &scene);
    if (status == XP_UI_HOME_VIEW_OK) {
      if (f.books) {
        const int next = frame.next.list.drawn;
        const int rows = winItems.size();
        LOG_DBG("LIB", "page tab=%d top=%d full=%d loaded=%d body=%d..%d next=%d title=%s", frame.snapshot.active_tab,
                windowStart, next, rows, frame.next.body.y, frame.next.body.y + frame.next.body.h,
                next < rows ? windowStart + next : -1, next < rows ? winTitles[next].c_str() : "<none>");
        LOG_DBG("LIB", "page first=%d title=%s", rows > 0 ? windowStart : -1,
                rows > 0 ? winTitles[0].c_str() : "<none>");
      }
      return true;
    }
    if (status != XP_UI_HOME_VIEW_CAPACITY || !frame.commands.grow()) return false;
  }
}

void LibraryListActivity::rustPublished() {
  if (!resourcesChanged)
    xp_ui_library_publish(&state, &frame.next, frame.snapshot.active_tab,
                          frame.snapshot.lists[frame.snapshot.active_tab].pending_scroll);
  xp_ui_home_list_header(&frame.hits, frame.headerControls, frame.headerCount);
  publishedHits = frame.hits;
}

void LibraryListActivity::paintHeader(const XpUiCommand& command) {
  const auto& theme = *sharedUiThemeCell().load(std::memory_order_acquire);
  fui::HeaderProps header;
  header.title = headerTitle();
  header.titleText = theme.titleText;
  header.titleText.align = theme.headerTitleAlign;
  header.sidePadding = theme.headerSidePadding;
  header.minTouchSize = theme.minTouchSize;
  header.styles = theme.popup;
  if (header.styles.normal.border.kind == fui::PaintKind::None && theme.headerUnderline > 0) {
    header.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    header.styles.normal.borderWidth = theme.headerUnderline;
  }
  header.trailingStyles = fui::plainStyles(fui::Paint::solid(fui::Color::Black));
  header.borderEdges = fui::EdgeBottom;
  GUI.applyHeaderStatus(renderer, header);
  if (command.style & 1) {
    header.leadingIcon = fui::bitmapFromIcon(icon_header_back_32);
    header.leadingAction = 6;
  }
  if (command.style & 2) {
    header.trailingIcon = fui::bitmapFromIcon(icon_search_32);
    header.trailingAction = 4;
  }
  if (command.style & 4) {
    header.trailingAdjacentIcon = fui::bitmapFromIcon(icon_refresh_cw_32);
    header.trailingAdjacentAction = 5;
  }
  const auto& r = command.rect;
  const auto band = BaseTheme::headerBand(renderer, {r.x, r.y, r.w, r.h}, header);
  const auto device = textTarget.deviceContext();
  const fui::InputSnapshot input{};
  fui::InteractionBuffer<24> controls;
  fui::Frame<24> surface(textTarget, device, input, controls);
  fui::header(surface, band, header);
  frame.headerCount = std::min<size_t>(controls.count(), 3);
  for (uint8_t i = 0; i < frame.headerCount; ++i) {
    const auto& c = controls.data()[i];
    frame.headerControls[i] = {
        {c.rect.x, c.rect.y, c.rect.width, c.rect.height}, c.value, static_cast<uint8_t>(c.action), {0, 0, 0}};
  }
}

XpUiPaint LibraryListActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* p, uint32_t h, uint32_t generation) -> uint8_t {
            auto& self = *static_cast<LibraryListActivity*>(p);
            if (generation != 1) return false;
            if (h == XP_UI_LIBRARY_HEADER || h == XP_UI_LIBRARY_FOOTER || h == XP_UI_READER_DELETE_ICON ||
                h == XP_UI_LIBRARY_UP || h == XP_UI_LIBRARY_DOWN)
              return true;
            if (h > 0 && h < 0x10000000u && (h - 1) % 4 == 3) return (h - 1) / 4 < self.winItems.size();
            return self.text(h) != nullptr;
          },
          [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
            auto& self = *static_cast<LibraryListActivity*>(p);
            const auto& r = c->rect;
            const auto h = c->resource;
            if (h == XP_UI_LIBRARY_HEADER) {
              self.paintHeader(*c);
              return;
            }
            if (h == XP_UI_LIBRARY_FOOTER) {
              const auto& n = self.frame.next;
              const auto labels =
                  self.mappedInput.mapLabels(n.back_home ? tr(STR_HOME) : tr(STR_BACK),
                                             n.confirm == 0   ? tr(STR_TOGGLE)
                                             : n.confirm == 1 ? tr(STR_SELECT)
                                                              : tr(STR_OPEN),
                                             n.search ? tr(STR_SEARCH) : tr(STR_DIR_UP), tr(STR_DIR_DOWN));
              GUI.drawButtonHints(self.renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
              return;
            }
            if (h > XP_UI_LIBRARY_HELP && h <= XP_UI_LIBRARY_HELP + 4) {
              GUI.drawHelpText(self.renderer, Rect{r.x, r.y, r.w, r.h}, self.text(h));
              return;
            }
            if (c->kind == XP_UI_BITMAP || h == XP_UI_LIBRARY_UP || h == XP_UI_LIBRARY_DOWN) {
              const auto previous = self.renderer.getClipRect();
              self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
              const auto ink = fui::Paint::solid(c->paint == 1 ? fui::Color::White : fui::Color::Black);
              if (h == XP_UI_LIBRARY_UP || h == XP_UI_LIBRARY_DOWN) {
                const bool down = h == XP_UI_LIBRARY_DOWN;
                if (down)
                  self.textTarget.triangle({r.x, r.y}, {static_cast<int16_t>(r.x + 6), r.y},
                                           {static_cast<int16_t>(r.x + 3), static_cast<int16_t>(r.y + 6)}, ink);
                else
                  self.textTarget.triangle({static_cast<int16_t>(r.x + 3), r.y}, {r.x, static_cast<int16_t>(r.y + 6)},
                                           {static_cast<int16_t>(r.x + 6), static_cast<int16_t>(r.y + 6)}, ink);
              } else {
                const auto icon = h == XP_UI_READER_DELETE_ICON ? fui::bitmapFromIcon(icon_trash_2_24)
                                                                : self.winItems[(h - 1) / 4].icon;
                self.textTarget.bitmap({r.x, r.y, r.w, r.h}, icon, fui::BitmapMode::Contain, ink);
              }
              self.renderer.setClipRect(previous[0], previous[1], previous[2], previous[3]);
            } else
              self.renderer.drawUiText(*c, clip, self.text(h));
          }};
}

void LibraryListActivity::rustSceneFailed() {
  nativeLongPress = nativeLongTouch = false;
  touchFeedback = 0;
  state.last_repeat = 0;
}

void LibraryListActivity::cancelRejectedInput(const XpUiInput& input) {
  if (!(input.released & XP_UI_BACK)) return;
  XpUiInput back{};
  back.released = XP_UI_BACK;
  back.held_ms = input.held_ms;
  XpUiEffect effect{};
  {
    ModelLock lock(*this);
    auto f = facts();
    f.long_confirm = f.long_touch = 0;
    effect = xp_ui_library_update(&state, &back, &f).effect;
  }
  if (effect.invalidate) requestUpdate();
  applyRustEffect(effect);
}
