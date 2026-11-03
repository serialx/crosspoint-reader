#include "RustAppResources.h"

#include <Bitmap.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Memory.h>
#include <Xtc.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <variant>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"
#include "util/BookProgress.h"

namespace {
XpAppText text(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.c_str()), value.size()}; }
XpAppText text(const char* value) { return {reinterpret_cast<const uint8_t*>(value), value ? strlen(value) : 0}; }
// Resource text is owned and NUL-terminated by Rust before layout or painting.
const char* cstr(XpAppText value) { return value.data ? reinterpret_cast<const char*>(value.data) : ""; }
std::string owned(XpAppText value) { return value.len ? std::string(cstr(value), value.len) : std::string(); }
Rect rect(XpUiRect r) { return Rect(r.x, r.y, r.w, r.h); }
XpUiRect rect(Rect r) {
  return {static_cast<int16_t>(r.x), static_cast<int16_t>(r.y), static_cast<int16_t>(r.width),
          static_cast<int16_t>(r.height)};
}
XpAppBook bookView(const RecentBook& book) {
  return {text(book.path), text(book.title), text(book.author), text(book.coverBmpPath)};
}

struct LibraryReader {
  library::LibraryIndexFile index;
  library::ClixRecord record;
  std::string path;
  uint32_t row = UINT32_MAX;
};
using BookReader = std::variant<Epub, Xtc>;
struct BitmapReader {
  HalFile file;
  Bitmap bitmap{file};
  bool parsed = false;
};
}  // namespace

const XpAppResources& rustAppResources(GfxRenderer& renderer, MappedInputManager& input) {
  static GfxRenderer* const target = &renderer;
  static MappedInputManager* const mapped = &input;
  static const XpAppResources resources{
      .settings_snapshot = []() -> XpAppHomeSettings {
        return {static_cast<uint8_t>(SETTINGS.pluginsEnabled != 0), static_cast<uint8_t>(OPDS_STORE.hasServers()),
                static_cast<uint8_t>(SETTINGS.libraryUseMetadata != 0),
                static_cast<uint8_t>(UITheme::getInstance().hasCoverGridHome())};
      },
      .recents_visit = [](XpAppBookSink sink, void* context) -> uint8_t {
        for (const auto& book : RECENT_BOOKS.getBooks()) {
          const auto view = bookView(book);
          if (!sink(context, &view)) return 0;
        }
        return 1;
      },
      .recent_missing = [](XpAppText path) -> uint8_t { return !Storage.exists(cstr(path)); },
      .recent_update =
          [](const XpAppBook* book) {
            RECENT_BOOKS.updateBook(owned(book->path), owned(book->title), owned(book->author), owned(book->cover));
          },
      .opds_count = []() -> uint32_t { return OPDS_STORE.getServers().size(); },
      .book_progress = [](XpAppText path) -> int32_t { return loadBookProgress(owned(path)); },
      .library_open = []() -> void* {
        auto reader = makeUniqueNoThrow<LibraryReader>();
        if (!reader) return nullptr;
        reader->index.open(library::libraryIndexPath());
        return reader.release();
      },
      .library_count = [](void* object) -> uint32_t {
        const auto& reader = *static_cast<LibraryReader*>(object);
        return reader.index.isOpen() ? reader.index.bookCount() : UINT32_MAX;
      },
      .library_row = [](void* object, uint32_t row, uint8_t metadata, XpAppBookSink sink, void* context) -> uint8_t {
        auto& reader = *static_cast<LibraryReader*>(object);
        if (row >= reader.index.bookCount()) return 0;
        if (!metadata) {
          reader.row = UINT32_MAX;
          if (!reader.index.readRecord(reader.index.ordinalForRow(library::SortOrder::RecentDesc, row),
                                       reader.record) ||
              !reader.index.readPath(reader.record, reader.path))
            return 0;
          reader.row = row;
          const XpAppBook view{text(reader.path), {}, {}, {}};
          return sink(context, &view);
        }
        if (metadata != 1 || row != reader.row) return 2;
        std::string title;
        std::string author;
        if (!reader.index.readTitle(reader.record, title) && !reader.index.readName(reader.record, title)) return 0;
        reader.index.readAuthor(reader.record, author);
        if (reader.index.ioFailed()) return 2;
        const XpAppBook view{{}, text(title), text(author), {}};
        return sink(context, &view);
      },
      .library_close = [](void* object) { delete static_cast<LibraryReader*>(object); },
      .library_rebuild = [](uint8_t metadata) -> uint8_t {
        library::BuildStats stats;
        return library::buildLibraryIndex("/", stats, metadata != 0);
      },
      .book_open = [](XpAppBookKind kind, XpAppText path) -> void* {
        switch (kind) {
          case XP_APP_BOOK_REFLOWABLE:
            return makeUniqueNoThrow<BookReader>(std::in_place_type<Epub>, owned(path), "/.crosspoint").release();
          case XP_APP_BOOK_XTC:
            return makeUniqueNoThrow<BookReader>(std::in_place_type<Xtc>, owned(path), "/.crosspoint").release();
          default:
            return nullptr;
        }
      },
      .book_thumb_path = [](void* object, int32_t height, XpAppTextSink sink, void* context) -> uint8_t {
        return std::visit(
            [&](auto& book) {
              const auto path = height > 0 ? book.getThumbBmpPath(height) : book.getThumbBmpPath();
              return sink(context, text(path));
            },
            *static_cast<BookReader*>(object));
      },
      .book_load = [](void* object, uint8_t buildMissing, uint8_t skipCss) -> uint8_t {
        return std::visit(
            [&](auto& book) {
              if constexpr (std::is_same_v<std::remove_reference_t<decltype(book)>, Epub>) {
                return book.load(buildMissing != 0, skipCss != 0);
              } else {
                return book.load();
              }
            },
            *static_cast<BookReader*>(object));
      },
      .book_generate_thumb = [](void* object, int32_t height) -> uint8_t {
        return std::visit([&](auto& book) { return book.generateThumbBmp(height); }, *static_cast<BookReader*>(object));
      },
      .book_generate_thumb_from_source = [](void* object, int32_t height) -> uint8_t {
        auto* book = std::get_if<Epub>(static_cast<BookReader*>(object));
        return book && book->generateThumbBmpFromSource(height);
      },
      .book_close = [](void* object) { delete static_cast<BookReader*>(object); },
      .path_exists = [](XpAppText path) -> uint8_t { return Storage.exists(cstr(path)); },
      .thumbnail_path = [](XpAppText base, int32_t height, XpAppTextSink sink, void* context) -> uint8_t {
        const auto path = UITheme::getCoverThumbPath(owned(base), height);
        return sink(context, text(path));
      },
      .bitmap_open = [](XpAppText path) -> void* {
        auto reader = makeUniqueNoThrow<BitmapReader>();
        if (!reader || !Storage.openFileForRead("HOME", cstr(path), reader->file)) return nullptr;
        reader->parsed = reader->bitmap.parseHeaders() == BmpReaderError::Ok;
        return reader.release();
      },
      .bitmap_info = [](void* object) -> XpAppBitmapInfo {
        const auto& reader = *static_cast<BitmapReader*>(object);
        return {reader.bitmap.getWidth(), reader.bitmap.getHeight(), static_cast<uint8_t>(reader.parsed)};
      },
      .bitmap_draw = [](void* object, XpUiRect bounds, int32_t shift) -> uint8_t {
        const auto& reader = *static_cast<BitmapReader*>(object);
        return reader.parsed && GUI.drawCoverThumbFill(*target, reader.bitmap, rect(bounds), shift);
      },
      .bitmap_close = [](void* object) { delete static_cast<BitmapReader*>(object); },
      .theme_snapshot =
          [](XpAppTheme* theme) {
            const auto& m = UITheme::getInstance().getMetrics();
            const auto& board = BoardConfig::ACTIVE;
            const auto fonts = uiScaleSpec();
            *theme = {};
            theme->screen = {0, 0, static_cast<int16_t>(target->getScreenWidth()),
                             static_cast<int16_t>(target->getScreenHeight())};
            theme->content = rect(UITheme::getContentArea(*target));
            theme->safe = rect(UITheme::getInstance().getScreenSafeArea(*target, true));
            theme->title_font = fonts.titleFontId;
            theme->small_font = fonts.smallFontId;
            theme->tiny_font = SMALL_FONT_ID;
            theme->body_font = fonts.bodyFontId;
            theme->top_padding = m.topPadding;
            theme->header_height = m.headerHeight;
            theme->home_top = m.homeTopPadding;
            theme->tile_height = m.homeCoverTileHeight;
            theme->cover_height = m.homeCoverHeight;
            theme->side_padding = m.contentSidePadding;
            theme->spacing = m.verticalSpacing;
            theme->menu_top = m.homeMenuTopOffset;
            theme->menu_height = m.menuRowHeight;
            theme->menu_gap = m.menuSpacing;
            theme->hints_height = m.buttonHintsHeight;
            theme->scroll_width = m.scrollBarWidth;
            theme->scroll_offset = m.scrollBarRightOffset;
            theme->header_side = m.headerSidePadding;
            theme->battery_height = m.batteryBarHeight;
            theme->corner = board.cornerRadius;
            theme->tab_height = m.coverGridTabBarHeight;
            theme->tab_height_pct = board.tabBarHeightPct;
            theme->tab_width_pct = board.tabBarWidthPct;
            theme->tab_offset = board.tabBarOffsetY;
            theme->theme = GUI.homeThemeKind();
            theme->continue_in_menu = m.homeContinueReadingInMenu;
            theme->columns = m.homeRecentBooksCount;
            theme->touch = BoardConfig::hasTouch();
            theme->body_bold = m.listTitleBold;
          },
      .translate = [](uint16_t id) -> XpAppText { return text(I18N.get(static_cast<StrId>(id))); },
      .header_draw =
          [](XpUiRect bounds, XpAppText title) {
            GUI.drawHeader(*target, rect(bounds), title.data ? cstr(title) : nullptr, nullptr, false);
          },
      .footer_draw =
          [](const XpAppText* labels) {
            const auto mappedLabels =
                mapped->mapLabels(cstr(labels[0]), cstr(labels[1]), cstr(labels[2]), cstr(labels[3]));
            GUI.drawButtonHints(*target, mappedLabels.btn1, mappedLabels.btn2, mappedLabels.btn3, mappedLabels.btn4);
          },
      .placeholder_draw = [](XpUiRect bounds) { GUI.drawCoverPlaceholder(*target, rect(bounds)); },
      .popup_draw = [](XpAppText message) -> XpUiRect { return rect(GUI.drawPopup(*target, cstr(message))); },
      .popup_progress = [](XpUiRect bounds, int32_t percent) { GUI.fillPopupProgress(*target, rect(bounds), percent); },
      .icon_draw =
          [](uint8_t icon, XpUiRect bounds) {
            static constexpr const uint8_t* icons[] = {BookIcon,     FolderIcon,    LibraryIcon, BlocksIcon,
                                                       TransferIcon, Settings2Icon, CoverIcon};
            if (icon < sizeof(icons) / sizeof(icons[0])) target->drawIcon(icons[icon], bounds.x, bounds.y, bounds.w);
          },
      .text_draw = [](const XpUiCommand* command, XpUiRect clip,
                      XpAppText value) { target->drawUiText(*command, clip, cstr(value)); },
      .text_width = [](XpAppText value, int32_t font, uint8_t style) -> int32_t {
        return target->getTextWidth(font, cstr(value), static_cast<EpdFontFamily::Style>(style));
      },
      .line_height = [](int32_t font) -> int32_t { return target->getLineHeight(font); },
      .text_wrap = [](XpAppText value, XpAppTextLayout layout, XpAppTextSink sink, void* context) -> uint8_t {
        const auto lines = target->wrappedText(layout.font, cstr(value), layout.width, layout.lines,
                                               static_cast<EpdFontFamily::Style>(layout.style));
        for (const auto& line : lines) {
          // cppcheck-suppress useStlAlgorithm // The raw loop keeps the firmware smaller than std::all_of.
          if (!sink(context, text(line))) return 0;
        }
        return 1;
      },
      .text_truncate = [](XpAppText value, XpAppTextLayout layout, XpAppTextSink sink, void* context) -> uint8_t {
        const auto line = target->truncatedText(layout.font, cstr(value), layout.width,
                                                static_cast<EpdFontFamily::Style>(layout.style));
        return sink(context, text(line));
      },
      .text_box_height = [](XpAppText value, XpAppTextLayout layout) -> int32_t {
        freeink::ui::GfxRendererTarget textTarget(*target);
        textTarget.setFont(0, layout.font);
        freeink::ui::TextStyle style;
        style.maxLines = layout.lines;
        style.bold = (layout.style & EpdFontFamily::BOLD) != 0;
        return freeink::ui::measureWrappedText(textTarget, cstr(value), style, layout.width).height;
      },
      .region_size = [](XpUiRect bounds) -> size_t {
        return target->getRegionByteSize(bounds.x, bounds.y, bounds.w, bounds.h);
      },
      .region_read = [](XpUiRect bounds, uint8_t* data, size_t capacity) -> uint8_t {
        return target->copyRegionToBuffer(bounds.x, bounds.y, bounds.w, bounds.h, data, capacity);
      },
      .region_write = [](XpUiRect bounds, const uint8_t* data, size_t capacity) -> uint8_t {
        return target->copyBufferToRegion(bounds.x, bounds.y, bounds.w, bounds.h, data, capacity);
      },
      .rounded_mask =
          [](XpUiRect bounds, uint8_t radius, uint8_t paint) {
            const auto color = paint == 2 ? Color::LightGray : (paint == 1 ? Color::White : Color::Black);
            target->maskRoundedRectOutsideCorners(bounds.x, bounds.y, bounds.w, bounds.h, radius, color);
          },
      .polygon_draw =
          [](const XpAppPoint* points, uint8_t count, uint8_t paint) {
            if (count > 5) return;
            int xs[5];
            int ys[5];
            for (uint8_t i = 0; i < count; ++i) {
              xs[i] = points[i].x;
              ys[i] = points[i].y;
            }
            // cppcheck-suppress uninitvar // fillPolygon reads only the count initialized elements.
            target->fillPolygon(xs, ys, count, paint != 1);
          },
      .paint_scene = [](const XpUiScene* scene, const XpUiPaint* paint) -> uint8_t {
        return target->drawUiScene(*scene, paint);
      },
      .refresh =
          [](uint8_t refresh) {
            switch (refresh) {
              case XP_UI_FAST:
                target->displayBuffer(HalDisplay::FAST_REFRESH);
                break;
              case XP_UI_HALF:
                target->displayBuffer(HalDisplay::HALF_REFRESH);
                break;
              case XP_UI_FULL:
                target->displayBuffer(HalDisplay::FULL_REFRESH);
                break;
              default:
                break;
            }
          },
      .buffer_allocate = [](XpAppMemoryDomain domain, size_t bytes) -> uint8_t* {
        switch (domain) {
          case XP_APP_MEMORY_DEFAULT:
            return static_cast<uint8_t*>(malloc(bytes));
          case XP_APP_MEMORY_PSRAM_ONLY:
            return HalMemory::allocatePsram(bytes).release();
          default:
            return nullptr;
        }
      },
      .buffer_release =
          [](XpAppMemoryDomain domain, uint8_t* data) {
            switch (domain) {
              case XP_APP_MEMORY_DEFAULT:
                free(data);
                break;
              case XP_APP_MEMORY_PSRAM_ONLY:
                HalMemory::PsramDeleter{}(data);
                break;
              default:
                break;
            }
          },
  };
  return resources;
}
