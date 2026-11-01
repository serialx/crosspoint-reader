#include "RustEpubResources.h"

#include <BlePageTurner.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <Epub/blocks/ImageBlock.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ProgressMapper.h>
#include <SdCardFont.h>
#include <SdCardFontSystem.h>
#include <TrustedTime.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BookmarkEntry.h"
#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderUtils.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "util/BookmarkFile.h"
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
#include "ReaderStripBuffers.h"
#endif

namespace {
XpAppText text(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
XpAppText text(const char* value) { return {reinterpret_cast<const uint8_t*>(value), strlen(value)}; }
// File paths come from the Rust owner's NUL-terminated Text storage.
const char* pathText(XpAppText value) { return value.data ? reinterpret_cast<const char*>(value.data) : ""; }
std::string owned(XpAppText value) {
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}

struct BookHandle {
  std::unique_ptr<Epub> loading;
  std::shared_ptr<Epub> book;

  Epub& value() const { return loading ? *loading : *book; }
};
struct NativeResources {
  std::optional<GfxRenderer::FrameBufferLoan> loan;
  std::optional<FontCacheManager::PrewarmScope> prewarm;
  PageMetadata metadata;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  ReaderStripBuffers strips;
#endif
};
NativeResources& resources(void* handle) { return *static_cast<NativeResources*>(handle); }
BookHandle& book(void* handle) { return *static_cast<BookHandle*>(handle); }
Section& section(void* handle) { return *static_cast<Section*>(handle); }
using Bookmarks = std::vector<BookmarkEntry>;
Bookmarks& bookmarkRows(void* handle) { return *static_cast<Bookmarks*>(handle); }
XpEpubBookmark bookmarkView(const BookmarkEntry& row) {
  return {text(row.xpath),
          text(row.summary),
          text(row.name),
          row.percentage,
          row.computedSpineIndex,
          row.computedChapterPageCount,
          row.computedChapterProgress,
          row.hasVisibleTextOffset,
          row.visibleTextOffset};
}

void readMargins(GfxRenderer& renderer, int32_t* out) {
  int margins[4]{};
  renderer.getOrientedViewableTRBL(&margins[0], &margins[1], &margins[2], &margins[3]);
  for (size_t i = 0; i < std::size(margins); ++i) out[i] = margins[i];
}

ReaderRenderSpec renderSpec(const XpEpubRenderSpec& value) {
  return {.fontId = value.font,
          .lineCompression = value.compression,
          .extraParagraphSpacing = value.paragraph_spacing != 0,
          .paragraphIndentSpaces = value.paragraph_indent,
          .characterSpacing = value.character_spacing,
          .wordSpacingPercent = value.word_spacing_percent,
          .paragraphAlignment = value.alignment,
          .viewportWidth = value.width,
          .viewportHeight = value.height,
          .hyphenationEnabled = value.hyphenation != 0,
          .embeddedStyle = value.embedded_style != 0,
          .imageRendering = value.images,
          .focusReadingEnabled = value.focus_reading != 0};
}

CrossPointPosition position(const XpEpubPosition& value) {
  CrossPointPosition result{value.spine, value.page, value.count};
  result.visibleTextOffset = value.visible_offset;
  result.hasVisibleTextOffset = value.has_visible_offset;
  result.paragraphIndex = value.paragraph;
  result.hasParagraphIndex = value.has_paragraph;
  result.liIndex = value.list_item;
  result.hasLiIndex = value.has_list_item;
  result.hasResolvedSpineIndex = value.resolved_spine;
  result.hasMappedPage = value.mapped_page;
  static_assert(sizeof(result.xpathAnchorId) == sizeof(value.anchor));
  memcpy(result.xpathAnchorId, value.anchor, sizeof(result.xpathAnchorId));
  return result;
}
void position(const CrossPointPosition& value, XpEpubPosition& result) {
  result = {};
  result.spine = value.spineIndex;
  result.page = value.pageNumber;
  result.count = value.totalPages;
  result.visible_offset = value.visibleTextOffset;
  result.has_visible_offset = value.hasVisibleTextOffset;
  result.paragraph = value.paragraphIndex;
  result.has_paragraph = value.hasParagraphIndex;
  result.list_item = value.liIndex;
  result.has_list_item = value.hasLiIndex;
  result.resolved_spine = value.hasResolvedSpineIndex;
  result.mapped_page = value.hasMappedPage;
  memcpy(result.anchor, value.xpathAnchorId, sizeof(result.anchor));
}
}  // namespace

const XpEpubLeaves& rustEpubResources(GfxRenderer& renderer, MappedInputManager& input) {
  static GfxRenderer* const target = &renderer;
  static MappedInputManager* const mapped = &input;
  static const XpEpubLeaves leaves{
      .resources_open = []() -> void* { return makeUniqueNoThrow<NativeResources>().release(); },
      .resources_close = [](void* handle) { delete static_cast<NativeResources*>(handle); },
      .render_margins = [](int32_t* margins) { readMargins(*target, margins); },
      .render_geometry =
          [](XpEpubGeometry* out) {
            *out = {};
            out->width = target->getScreenWidth();
            out->height = target->getScreenHeight();
            readMargins(*target, out->margins);
            out->status_height = UITheme::getInstance().getStatusBarHeight();
            out->progress_height = UITheme::getInstance().getProgressBarHeight();
            out->status_margin = UITheme::getInstance().getMetrics().statusBarVerticalMargin;
            out->title_mode = SETTINGS.statusBarSpec().titleMode;
            out->has_framebuffer = target->hasFrameBuffer();
            out->async_refresh = target->supportsAsyncRefresh();
            out->xteink_panel = gpio.isXteinkDevice() || BoardConfig::isX4Pro() || BoardConfig::isX4Classic() ||
                                BoardConfig::isEegoA4();
            out->a4 = BoardConfig::isEegoA4();
            out->has_ttf = !target->getTtfFonts().empty();
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
            out->strip_enabled = true;
#endif
          },
      .render_restore_bw = [](uint8_t resync) { target->restoreBwBuffer(resync != 0); },
      .render_discard_bw = [] { target->discardStoredBwBuffer(); },
      .render_orientation = [](uint8_t orientation) { ReaderUtils::applyOrientation(*target, orientation); },
      .loan_begin = [](void* handle) -> uint8_t {
        auto& loan = resources(handle).loan;
        assert(!loan);
        loan.emplace(*target);
        return 1;
      },
      .loan_end =
          [](void* handle) {
            if (auto& loan = resources(handle).loan) loan->end();
          },
      .loan_drop = [](void* handle) { resources(handle).loan.reset(); },
      .loan_count = [] { return target->frameBufferLoanCount(); },
      .strip_release_idle = [](void* handle) -> uint8_t {
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        return resources(handle).strips.releaseIdle();
#else
        (void)handle;
        return false;
#endif
      },
      .book_open = [](XpAppText path) -> void* {
        auto handle = makeUniqueNoThrow<BookHandle>();
        if (!handle) return nullptr;
        handle->loading = makeUniqueNoThrow<Epub>(owned(path), "/.crosspoint");
        return handle->loading ? handle.release() : nullptr;
      },
      .book_load = [](void* handle, uint8_t buildMissing, uint8_t skipCss) -> uint8_t {
        auto& resource = book(handle);
        if (!resource.value().load(buildMissing != 0, skipCss != 0)) return 0;
        if (resource.loading) resource.book = std::move(resource.loading);
        return 1;
      },
      .book_close = [](void* handle) { delete static_cast<BookHandle*>(handle); },
      .book_size = [](void* handle) -> uint32_t { return book(handle).value().getBookSize(); },
      .book_spine_count = [](void* handle) -> int32_t { return book(handle).value().getSpineItemsCount(); },
      .book_toc_count = [](void* handle) -> int32_t { return book(handle).value().getTocItemsCount(); },
      .book_path = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
        return sink(context, text(book(handle).value().getPath()));
      },
      .book_cache_path = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
        return sink(context, text(book(handle).value().getCachePath()));
      },
      .book_title = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
        return sink(context, text(book(handle).value().getTitle()));
      },
      .book_author = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
        return sink(context, text(book(handle).value().getAuthor()));
      },
      .book_language = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
        return sink(context, text(book(handle).value().getLanguage()));
      },
      .book_thumbnail = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
        const auto value = book(handle).value().getThumbBmpPath();
        return sink(context, text(value));
      },
      .book_spine = [](void* handle, int32_t index, XpEpubSpineSink sink, void* context) -> uint8_t {
        const auto value = book(handle).value().getSpineItem(index);
        const XpEpubSpine view{text(value.href), value.cumulativeSize};
        return sink(context, &view);
      },
      .book_toc = [](void* handle, int32_t index, XpEpubTocSink sink, void* context) -> uint8_t {
        const auto value = book(handle).value().getTocItem(index);
        const XpEpubToc view{text(value.title), text(value.anchor), value.spineIndex, value.level};
        return sink(context, &view);
      },
      .book_spine_for_toc = [](void* handle, int32_t index) -> int32_t {
        return book(handle).value().getSpineIndexForTocIndex(index);
      },
      .book_toc_for_spine = [](void* handle, int32_t index) -> int32_t {
        return book(handle).value().getTocIndexForSpineIndex(index);
      },
      .book_cumulative_size = [](void* handle, int32_t spine) -> uint32_t {
        return book(handle).value().getCumulativeSpineItemSize(spine);
      },
      .book_spine_for_size = [](void* handle, uint32_t bytes) -> int32_t {
        return book(handle).value().getSpineIndexForSize(bytes);
      },
      .book_text_reference = [](void* handle) -> int32_t {
        return book(handle).value().getSpineIndexForTextReference();
      },
      .book_progress = [](void* handle, int32_t spine,
                          float fraction) { return book(handle).value().calculateProgress(spine, fraction); },
      .book_resolve_href = [](void* handle, XpAppText href) -> int32_t {
        return book(handle).value().resolveHrefToSpineIndex(owned(href));
      },
      .book_setup_cache = [](void* handle) { book(handle).value().setupCacheDir(); },
      .book_clear_cache = [](void* handle) { book(handle).value().clearCache(); },
      .book_install_extractor =
          [](void* handle) {
            ImageBlock::setExtractor(&book(handle).value(), [](void* raw, const char* source, const char* destination) {
              return static_cast<Epub*>(raw)->extractItemToFile(source, destination);
            });
          },
      .book_clear_extractor = [] { ImageBlock::setExtractor(nullptr, nullptr); },
      .book_protection_error = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
        return sink(context, text(book(handle).value().getProtectionError()));
      },
      .progress_to_reader =
          [](void* handle, const XpEpubSavedProgress* saved, int32_t spine, int32_t count, XpEpubPosition* out) {
            const auto value = ProgressMapper::toCrossPoint(book(handle).book, {owned(saved->xpath), saved->percentage},
                                                            *target, spine, count);
            position(value, *out);
          },
      .progress_to_saved = [](void* handle, const XpEpubPosition* source, XpEpubSavedProgressSink sink,
                              void* context) -> uint8_t {
        const auto value = ProgressMapper::toSavedProgress(book(handle).book, position(*source));
        const XpEpubSavedProgress view{text(value.xpath), value.percentage};
        return sink(context, &view);
      },
      .progress_save = [](void* handle, const XpEpubPosition* value) -> uint8_t {
        return EpubReaderUtils::saveProgress(
            book(handle).value(), value->spine, value->page, value->count,
            value->has_visible_offset ? std::optional<uint32_t>(value->visible_offset) : std::nullopt);
      },
      .section_open = [](void* handle, int32_t spine) -> void* {
        return makeUniqueNoThrow<Section>(book(handle).book, spine, *target).release();
      },
      .section_close = [](void* handle) { delete static_cast<Section*>(handle); },
      .section_page_count = [](void* handle) { return section(handle).pageCount; },
      .section_estimated_pages = [](void* handle) { return section(handle).estimatedTotalPages(); },
      .section_building = [](void* handle) -> uint8_t { return section(handle).isBuilding(); },
      .section_complete = [](void* handle) -> uint8_t { return section(handle).isBuildComplete(); },
      .section_partial = [](void* handle) -> uint8_t { return section(handle).isPartial(); },
      .section_load_cache = [](void* handle, const XpEpubRenderSpec* spec) -> uint8_t {
        return section(handle).loadSectionFile(renderSpec(*spec));
      },
      .section_create_cache = [](void* handle, const XpEpubRenderSpec* spec, XpEpubBuildPopup popup,
                                 void* context) -> uint8_t {
        return section(handle).createSectionFile(renderSpec(*spec), [popup, context] { popup(context); });
      },
      .section_start_build = [](void* handle, const XpEpubRenderSpec* spec, XpEpubBuildPopup popup, void* context)
          -> uint8_t { return section(handle).startBuild(renderSpec(*spec), [popup, context] { popup(context); }); },
      .section_build_more = [](void* handle, int32_t maxPages, uint8_t yieldOnInput) -> uint8_t {
        return section(handle).buildSomeMore(maxPages, yieldOnInput ? +[] { return gpio.rawInputActive(); } : nullptr);
      },
      .section_abandon_build = [](void* handle) { section(handle).abandonBuild(); },
      .section_clear_cache = [](void* handle) -> uint8_t { return section(handle).clearCache(); },
      .section_html_cached = [](void* handle) -> uint8_t { return section(handle).hasHtmlCache(); },
      .section_find_anchor = [](void* handle, XpAppText anchor, uint16_t* page) -> uint8_t {
        const auto value = section(handle).findAnchor(owned(anchor));
        if (value) *page = *value;
        return value.has_value();
      },
      .section_cached_anchor = [](void* handle, XpAppText anchor, uint16_t* page) -> uint8_t {
        const auto value = section(handle).getPageForAnchor(owned(anchor));
        if (value) *page = *value;
        return value.has_value();
      },
      .section_toc_for_page = [](void* handle, int32_t page) -> int32_t {
        return section(handle).getTocIndexForPage(page);
      },
      .section_page_for_offset = [](void* handle, uint32_t offset, uint8_t preferFirst, uint16_t* page) -> uint8_t {
        const auto value = section(handle).getPageForVisibleTextOffset(offset, preferFirst != 0);
        if (value) *page = *value;
        return value.has_value();
      },
      .section_offset_for_page = [](void* handle, uint16_t page, uint32_t* offset) -> uint8_t {
        const auto value = section(handle).getVisibleTextOffsetForPage(page);
        if (value) *offset = *value;
        return value.has_value();
      },
      .section_paragraph_for_page = [](void* handle, uint16_t page, uint16_t* paragraph) -> uint8_t {
        const auto value = section(handle).getParagraphIndexForPage(page);
        if (value) *paragraph = *value;
        return value.has_value();
      },
      .section_reached_offset = [](void* handle, uint32_t offset) -> uint8_t {
        return section(handle).buildReachedVisibleTextOffset(offset);
      },
      .section_load_page = [](void* handle, int32_t page) -> void* { return section(handle).loadPage(page).release(); },
      .section_page_text = [](void* handle, int32_t page, XpAppTextSink sink, void* context) -> uint8_t {
        const auto value = section(handle).getTextFromSectionFile(page);
        return sink(context, text(value));
      },
      .page_close = [](void* handle) { delete static_cast<Page*>(handle); },
      .metadata_assign = [](void* handle, void* page) -> uint8_t {
        return resources(handle).metadata.assign(*static_cast<Page*>(page));
      },
      .metadata_clear = [](void* handle) { resources(handle).metadata.clear(); },
      .metadata_footnotes = [](void* handle) -> uint16_t { return resources(handle).metadata.footnotes().size(); },
      .metadata_links = [](void* handle) -> uint16_t { return resources(handle).metadata.links().size(); },
      .metadata_footnote = [](void* handle, uint16_t index, XpEpubFootnoteSink sink, void* context) -> uint8_t {
        const auto rows = resources(handle).metadata.footnotes();
        if (index >= rows.size()) return 0;
        const XpEpubFootnote view{text(rows[index].number), text(rows[index].href)};
        return sink(context, &view);
      },
      .metadata_link = [](void* handle, uint16_t index, XpEpubLinkSink sink, void* context) -> uint8_t {
        const auto rows = resources(handle).metadata.links();
        if (index >= rows.size()) return 0;
        const auto& row = rows[index];
        const XpEpubLink view{text(row.href), row.x, row.y, row.width, row.height};
        return sink(context, &view);
      },
      .image_clear_failures = [] { ImageBlock::clearRenderFailures(); },
      .image_release_cache = [] { ImageBlock::releaseRenderCache(); },
      .fonts_ensure_loaded = [] { sdFontSystem.ensureLoaded(*target); },
      .fonts_release_caches =
          [] {
            if (auto* cache = target->getFontCacheManager()) cache->releaseSdFontCaches();
          },
      .fonts_prewarm_begin = [](void* handle) -> uint8_t {
        auto* cache = target->getFontCacheManager();
        if (!cache) return 0;
        auto& prewarm = resources(handle).prewarm;
        assert(!prewarm);
        prewarm.emplace(*cache);
        return 1;
      },
      .fonts_prewarm_finish_scan = [](void* handle) { resources(handle).prewarm->endScanAndPrewarm(); },
      .fonts_prewarm_drop = [](void* handle) { resources(handle).prewarm.reset(); },
      .fonts_take_corrupt_notice = []() -> uint8_t { return SdCardFont::takeCorruptGlyphNotice(); },
      .input_pressed = [](uint32_t button) -> uint8_t {
        using Button = MappedInputManager::Button;
        constexpr Button buttons[] = {Button::Back, Button::Confirm, Button::Left, Button::Right,
                                      Button::Up,   Button::Down,    Button::Power};
        assert(button < std::size(buttons));
        return mapped->isPressed(buttons[button]);
      },
      .input_raw_contact = []() -> uint8_t { return gpio.rawInputActive(); },
      .file_open_read = [](XpAppText path) -> void* {
        auto file = makeUniqueNoThrow<HalFile>();
        if (!file || !Storage.openFileForRead("ERS", pathText(path), *file)) return nullptr;
        return file.release();
      },
      .file_open_write = [](XpAppText path) -> void* {
        auto file = makeUniqueNoThrow<HalFile>();
        if (!file || !Storage.openFileForWrite("ERS", pathText(path), *file)) return nullptr;
        return file.release();
      },
      .file_read = [](void* file, uint8_t* bytes, uint32_t length) -> int32_t {
        return static_cast<HalFile*>(file)->read(bytes, length);
      },
      .file_write = [](void* file, const uint8_t* bytes, uint32_t length) -> uint32_t {
        return static_cast<HalFile*>(file)->write(bytes, length);
      },
      .file_flush = [](void* file) { static_cast<HalFile*>(file)->flush(); },
      .file_close = [](void* file) { delete static_cast<HalFile*>(file); },
      .path_remove = [](XpAppText path) -> uint8_t { return Storage.remove(pathText(path)); },
      .path_rename = [](XpAppText from, XpAppText to) -> uint8_t {
        return Storage.rename(pathText(from), pathText(to));
      },
      .path_mkdir = [](XpAppText path) -> uint8_t { return Storage.mkdir(pathText(path)); },
      .native_path_hash = [](XpAppText path) -> uint64_t { return std::hash<std::string>{}(owned(path)); },
      .heap_snapshot = [](XpEpubHeap* out) { *out = {ESP.getFreeHeap(), ESP.getMaxAllocHeap()}; },
      .radio_status =
          [](XpEpubRadio* out) {
            const auto status = bleturner::status();
            *out = {};
            out->compiled_in = status.compiledIn;
            out->running = status.running;
            out->starting = status.starting;
            out->stopping = status.stopping;
            out->note = static_cast<uint8_t>(bleturner::linkNote());
          },
      .radio_before_build = []() -> uint8_t {
        return bleturner::beforeChapterBuild() != bleturner::BuildRelease::NotHeld;
      },
      .radio_settle_start = [] { bleturner::settleStart(); },
      .radio_holds_heap = []() -> uint8_t { return bleturner::holdsHeap(); },
      .radio_after_paint = [] { bleturner::afterPaint(); },
      .radio_switch_off = [] { bleturner::switchOff(); },
      .radio_request_connect = [] { bleturner::requestConnect(); },
      .trusted_now = [] { return trustedtime::trustedNow(); },
      .trusted_sync = [](uint32_t timeout) -> uint8_t { return trustedtime::syncNow(timeout); },
      .wifi_connected = []() -> uint8_t { return WiFi.status() == WL_CONNECTED; },
      .wifi_disconnect = [](uint8_t off) { WiFi.disconnect(off != 0); },
      .bookmarks_open = []() -> void* { return makeUniqueNoThrow<Bookmarks>().release(); },
      .bookmarks_close = [](void* handle) { delete static_cast<Bookmarks*>(handle); },
      .bookmarks_clear = [](void* handle) { bookmarkRows(handle).clear(); },
      .bookmarks_load = [](void* handle, void* bookHandle) -> uint8_t {
        return BookmarkFile::load(book(bookHandle).value().getPath(), bookmarkRows(handle));
      },
      .bookmarks_save = [](void* handle, void* bookHandle) -> uint8_t {
        return BookmarkFile::save(book(bookHandle).value().getPath(), bookmarkRows(handle));
      },
      .bookmarks_count = [](void* handle) -> uint32_t { return bookmarkRows(handle).size(); },
      .bookmarks_row = [](void* handle, uint32_t index, XpEpubBookmarkSink sink, void* context) -> uint8_t {
        const auto& rows = bookmarkRows(handle);
        if (index >= rows.size()) return 0;
        const auto row = bookmarkView(rows[index]);
        return sink(context, &row);
      },
      .bookmarks_retain = [](void* handle, uint8_t (*keep)(void*, const XpEpubBookmark*), void* context) -> uint32_t {
        auto& rows = bookmarkRows(handle);
        const size_t before = rows.size();
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [&](const BookmarkEntry& entry) {
                                    const auto row = bookmarkView(entry);
                                    return keep(context, &row) == 0;
                                  }),
                   rows.end());
        return before - rows.size();
      },
      .bookmarks_insert_front = [](void* handle, const XpEpubBookmark* row) -> uint8_t {
        BookmarkEntry entry;
        entry.xpath = owned(row->xpath);
        entry.summary = owned(row->summary);
        entry.name = owned(row->name);
        entry.percentage = row->percentage;
        entry.computedSpineIndex = row->spine;
        entry.computedChapterPageCount = row->page_count;
        entry.computedChapterProgress = row->chapter_page;
        entry.hasVisibleTextOffset = row->has_offset != 0;
        entry.visibleTextOffset = row->offset;
        bookmarkRows(handle).insert(bookmarkRows(handle).begin(), std::move(entry));
        return 1;
      },
      .clippings_load = [](void* handle) -> uint8_t {
        const auto& value = book(handle).value();
        return CLIPPINGS.loadForBook(value.getPath(), value.getTitle(), value.getAuthor(), "epub");
      },
      .clippings_unload = [] { CLIPPINGS.unload(); },
      .clippings_count = []() -> uint32_t { return CLIPPINGS.clippingCount(); },
      .clippings_move_book = [](XpAppText from, XpAppText to) -> uint8_t {
        return ClippingStore::moveBook(owned(from), owned(to));
      },
      .recents_update_path =
          [](XpAppText from, XpAppText to, XpAppText oldCache, XpAppText newCache) {
            RECENT_BOOKS.updatePath(owned(from), owned(to), owned(oldCache), owned(newCache));
          },
      .state_open_path = [](XpAppTextSink sink, void* context) -> uint8_t {
        return sink(context, text(APP_STATE.openEpubPath));
      },
      .state_set_open_path =
          [](XpAppText path) { APP_STATE.openEpubPath.assign(reinterpret_cast<const char*>(path.data), path.len); },
      .state_save = [] { APP_STATE.saveToFile(); },
      .log =
          [](uint8_t severity, XpAppText origin, XpAppText message) {
            char tag[16]{};
            assert(origin.len < sizeof(tag));
            memcpy(tag, origin.data, origin.len);
            const auto* bytes = reinterpret_cast<const char*>(message.data);
            switch (severity) {
              case 0:
                LOG_DBG(tag, "%.*s", static_cast<int>(message.len), bytes);
                break;
              case 1:
                LOG_ERR(tag, "%.*s", static_cast<int>(message.len), bytes);
                break;
              case 2:
                LOG_INF(tag, "%.*s", static_cast<int>(message.len), bytes);
                break;
              default:
                break;
            }
          },
  };
  return leaves;
}
