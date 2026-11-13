#include "RustEpubResources.h"

#include <BidiUtils.h>
#include <BlePageTurner.h>
#include <BoardConfig.h>
#include <BookKey.h>
#include <BuildScratch.h>
#include <ContentProtection.h>
#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>

// GfxRenderer.h selects the direct provider for firmware.
// Scalar metric callbacks below remain for XP_APP_TEST fixtures.
// Their stub renderer does not define XP_NATIVE_TEXT_MEASUREMENT.
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <InflateStream.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>
#include <Memory.h>
#include <ReaderProtection.h>
#ifdef XP_APP_TEST
#include <SdCardFont.h>
#endif
#include <SdCardFontSystem.h>
#include <TrustedTime.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ClipSelectionActivity.h"
#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DefinitionPayload.h"
#include "DictionaryUiBindings.h"
#include "EndOfBookOptions.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "RustEpubPage.h"
#include "RustReaderDrawing.h"
#include "RustReaderLookup.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/OwnedLegacyActivity.h"
#include "activities/RenderLock.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "components/HeaderBackTapTarget.h"
#include "components/OptionPopup.h"
#include "components/RustInputSnapshot.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/HomeButtonInput.h"
#include "util/PluginEvents.h"
#include "util/QrUtils.h"
#include "util/SlideSwitch.h"

const XpParaAllocator* paragraphAllocator();
const XpParaAllocator* paragraphPhysicalAllocator();

namespace {
uint32_t physicalRenderQuery(GfxRenderer& renderer, uint8_t query) {
  switch (query) {
    case XP_EPUB_RENDER_IS_BW:
      return renderer.getRenderMode() == GfxRenderer::BW;
    case XP_EPUB_RENDER_IS_X3:
      return gpio.deviceIsX3();
    case XP_EPUB_RENDER_IS_UC8279:
      return display.getController() == HalDisplay::Controller::UC8279;
    case XP_EPUB_RENDER_ABSOLUTE_SUPPORTED:
      return renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
    case XP_EPUB_RENDER_OVERLAY_CAPS:
    case XP_EPUB_RENDER_ABSOLUTE_CAPS: {
      const auto caps =
          renderer.grayscaleCapabilities(query == XP_EPUB_RENDER_ABSOLUTE_CAPS ? HalDisplay::GrayscaleMode::Absolute
                                                                               : HalDisplay::GrayscaleMode::Overlay);
      return (caps.stripUploads ? 1u : 0u) | (caps.base == HalDisplay::GrayscaleBase::Combined ? 2u : 0u) |
             (caps.asyncBase ? 4u : 0u);
    }
    case XP_EPUB_RENDER_STRIP_BOARD:
      return display.getController() == HalDisplay::Controller::UC8253 ||
             (BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4 &&
              display.getController() == HalDisplay::Controller::SSD1677);
    case XP_EPUB_RENDER_WIDTH_BYTES:
      return renderer.getDisplayWidthBytes();
    case XP_EPUB_RENDER_HEIGHT:
      return renderer.getDisplayHeight();
    case XP_EPUB_RENDER_FREE_HEAP:
      return ESP.getFreeHeap();
    case XP_EPUB_RENDER_MAX_BLOCK:
      return ESP.getMaxAllocHeap();
    case XP_EPUB_RENDER_MODE:
      switch (renderer.getRenderMode()) {
        case GfxRenderer::BW:
          return 0;
        case GfxRenderer::GRAYSCALE_LSB:
          return 1;
        case GfxRenderer::GRAYSCALE_MSB:
          return 2;
      }
      break;
    case XP_EPUB_RENDER_INVERTED:
      return display.isInverted();
    case XP_EPUB_RENDER_WIDTH:
      return renderer.getDisplayWidth();
  }
  return 0;
}

uint32_t physicalRenderOperation(GfxRenderer& renderer, const XpEpubPhysicalOp& op) {
  const auto refresh = op.mode == XP_UI_HALF ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  switch (op.kind) {
    case XP_EPUB_RENDER_CLEAR:
      renderer.clearScreen(op.value);
      break;
    case XP_EPUB_RENDER_DISPLAY:
      renderer.displayBuffer(refresh);
      break;
    case XP_EPUB_RENDER_DISPLAY_ASYNC:
      renderer.displayBufferAsync(refresh);
      break;
    case XP_EPUB_RENDER_GRAY_BASE:
      renderer.displayGrayscaleBase(refresh);
      break;
    case XP_EPUB_RENDER_ABSOLUTE_BASE:
      return renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, refresh);
    case XP_EPUB_RENDER_PRECONDITION:
      renderer.preconditionGrayscale();
      break;
    case XP_EPUB_RENDER_MODE_SET:
      switch (op.mode) {
        case 0:
          renderer.setRenderMode(GfxRenderer::BW);
          break;
        case 1:
          renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
          break;
        case 2:
          renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
          break;
      }
      break;
    case XP_EPUB_RENDER_STRIP_BEGIN:
      renderer.beginStripTarget(op.buffer, op.y, op.rows);
      break;
    case XP_EPUB_RENDER_STRIP_END:
      renderer.endStripTarget();
      break;
    case XP_EPUB_RENDER_WAIT:
      renderer.waitRefreshComplete();
      break;
    case XP_EPUB_RENDER_WRITE_PLANE:
      renderer.writeGrayscalePlaneStrip(op.mode == 1, op.buffer, op.y, op.rows);
      break;
    case XP_EPUB_RENDER_GRAY_DISPLAY:
      renderer.displayGrayBuffer();
      break;
    case XP_EPUB_RENDER_CLEANUP:
      renderer.cleanupGrayscaleWithFrameBuffer();
      break;
    case XP_EPUB_RENDER_STORE_BW:
      return renderer.storeBwBuffer();
    case XP_EPUB_RENDER_RESTORE_BW:
      renderer.restoreBwBuffer();
      break;
    case XP_EPUB_RENDER_COPY_LSB:
      renderer.copyGrayscaleLsbBuffers();
      break;
    case XP_EPUB_RENDER_COPY_MSB:
      renderer.copyGrayscaleMsbBuffers();
      break;
    case XP_EPUB_RENDER_COPY_STATUS:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
      renderer.copyStatusToStrip(static_cast<int32_t>(op.value), op.mode != 0);
#endif
      break;
  }
  return 0;
}

void physicalImageWindow(uint8_t* bytes, uint32_t length) {
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  ImageBlock::setRenderReadWindow(bytes, length);
#else
  (void)bytes;
  (void)length;
#endif
}
uint8_t physicalDisplaySource(XpDisplaySource* source) {
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  return display.setFrameSource(source);
#else
  (void)source;
  return 0;
#endif
}
uint8_t* physicalFramebuffer(GfxRenderer& renderer, uint32_t* bytes) {
  *bytes = renderer.getBufferSize();
  return renderer.getFrameBuffer();
}
#ifdef XP_APP_TEST
uint8_t physicalPrewarmBegin(GfxRenderer& renderer, XpEpubFontScan* scan) {
  if (!renderer.getFontCacheManager()) return 0;
  scan->owner = renderer.getFontMap().raw();
  scan->active = 1;
  xp_font_frame_begin(static_cast<const XpLoadedFonts*>(scan->owner));
  return 1;
}
void physicalPrewarmFinish(XpEpubFontScan* scan) {
  if (scan->active) xp_font_frame_finish(static_cast<const XpLoadedFonts*>(scan->owner));
}
void physicalPrewarmDrop(XpEpubFontScan* scan) {
  if (!scan->active) return;
  const auto* owner = static_cast<const XpLoadedFonts*>(scan->owner);
  *scan = {};
  xp_font_frame_end(owner);
}
#endif

static const GfxRenderer& sectionRenderer(void* raw) { return *static_cast<GfxRenderer*>(raw); }
[[maybe_unused]] int32_t textAdvance(void* context, int32_t font, XpParaSpan text, uint8_t style, int8_t tracking) {
  return sectionRenderer(context).getTextAdvanceX(font, text.len ? reinterpret_cast<const char*>(text.data) : "",
                                                  static_cast<EpdFontFamily::Style>(style), tracking);
}
void prepareText(void* context, int32_t font, const XpParaSpan* spans, uint32_t count, uint8_t space, uint8_t hyphen,
                 uint8_t styles, uint8_t small) {
  std::vector<const char*> segments;
  std::vector<size_t> lengths;
  segments.reserve(count);
  lengths.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    segments.push_back(reinterpret_cast<const char*>(spans[i].data));
    lengths.push_back(spans[i].len);
  }
  sectionRenderer(context).ensureSdCardFontReady(font, segments.data(), lengths.data(), count, space, hyphen, styles,
                                                 small);
}
[[maybe_unused]] uint8_t startsRtl(XpParaSpan text, int32_t depth) {
  return BidiUtils::startsWithRtl(std::string_view(reinterpret_cast<const char*>(text.data), text.len), depth);
}
[[maybe_unused]] uint8_t visualOrder(XpParaSpan text, const uint16_t* offsets, uint32_t count, uint8_t rtl,
                                     uint16_t* order) {
  return BidiUtils::computeVisualWordOrder({reinterpret_cast<const char*>(text.data), text.len}, {offsets, count}, rtl,
                                           {order, count});
}
XpParaMetrics sectionMetrics{
    nullptr,
    nullptr,  // Layout tracing is installed only by the host instrumenter.
#ifdef XP_NATIVE_TEXT_MEASUREMENT
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
#ifdef XP_APP_TEST
    prepareText,
#else
    xp_sd_font_prepare_spans,
#endif
    nullptr,
    nullptr,
#elif defined(XP_APP_TEST)
    textAdvance,
    [](void* c, int32_t f, uint8_t s) -> int32_t {
      return sectionRenderer(c).getSpaceWidth(f, static_cast<EpdFontFamily::Style>(s));
    },
    [](void* c, int32_t f, uint32_t a, uint32_t b, uint8_t s, int8_t t) -> int32_t {
      return sectionRenderer(c).getKerning(f, a, b, static_cast<EpdFontFamily::Style>(s), t);
    },
    [](void* c, int32_t f, uint32_t a, uint32_t b, uint8_t s) -> int32_t {
      return sectionRenderer(c).getSpaceAdvance(f, a, b, static_cast<EpdFontFamily::Style>(s));
    },
    [](void* c, int32_t f) -> int32_t { return sectionRenderer(c).getFontAscenderSize(f); },
    [](void* c, int32_t f) -> uint8_t { return sectionRenderer(c).isSdCardFont(f); },
    prepareText,
    startsRtl,
    visualOrder,
#endif
#ifdef XP_NATIVE_TEXT_MEASUREMENT
#ifdef XP_APP_TEST
    [](const void* ctx) -> XpNativeProvider { return sectionRenderer(const_cast<void*>(ctx)).textProvider(); },
#else
    xp_font_registry_provider,
#endif
#elif defined(XP_APP_TEST)
    nullptr,
#endif
    nullptr};

// Persisted input settings consumed by the ordinary Rust reader.
static_assert(static_cast<uint32_t>(HomeButtonAction::Footnotes) == 4);
static_assert(static_cast<uint32_t>(HomeButtonAction::Sync) == 6);
static_assert(static_cast<uint32_t>(HomeButtonAction::Bookmark) == 7);
static_assert(static_cast<uint32_t>(HomeButtonAction::Dictionary) == 8);
static_assert(static_cast<uint32_t>(HomeButtonAction::ReaderMenu) == 9);
static_assert(static_cast<uint32_t>(HomeButtonAction::CreateClipping) == 11);
static_assert(static_cast<uint32_t>(HomeButtonAction::Ignore) == 1);
static_assert(static_cast<uint32_t>(CrossPointSettings::LP_MENU_KOSYNC) == 0);
static_assert(static_cast<uint32_t>(CrossPointSettings::LP_MENU_BOOKMARK) == 2);
static_assert(static_cast<uint32_t>(CrossPointSettings::LP_MENU_DICTIONARY) == 3);
static_assert(static_cast<uint32_t>(CrossPointSettings::LP_MENU_CREATE_CLIPPING) == 5);
static_assert(static_cast<uint32_t>(CrossPointSettings::LP_MENU_FOOTNOTES) == 6);
static_assert(static_cast<uint32_t>(CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) == 4);
static_assert(static_cast<uint32_t>(CrossPointSettings::SHORT_PWRBTN::CREATE_CLIPPING) == 6);
// Cardinalities and toolbar setting consumed by Rust label/selection tables.
static_assert(CrossPointSettings::LINE_COMPRESSION_COUNT == 4);
static_assert(CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT == 5);
static_assert(CrossPointSettings::ORIENTATION_COUNT == 4);
static_assert(CrossPointSettings::FONT_FAMILY_COUNT == 1);
static_assert(CrossPointSettings::READER_MENU_TOOLBAR == 1);
// Raw mapped directions are translated to shared XpUiSwipe by ReaderUtils.
static_assert(static_cast<uint8_t>(MappedInputManager::SwipeDir::None) == 0);
static_assert(static_cast<uint8_t>(MappedInputManager::SwipeDir::Left) == 1);
static_assert(static_cast<uint8_t>(MappedInputManager::SwipeDir::Right) == 2);
static_assert(static_cast<uint8_t>(MappedInputManager::SwipeDir::Up) == 3);
static_assert(static_cast<uint8_t>(MappedInputManager::SwipeDir::Down) == 4);

XpAppText text(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
XpAppText text(const char* value) { return {reinterpret_cast<const uint8_t*>(value), strlen(value)}; }
// C-string consumers borrow terminated owner storage; spans use owned() or view().
const char* pathText(XpAppText value) {
  if (!value.len) return "";
  assert(value.data && value.data[value.len] == 0);
  return reinterpret_cast<const char*>(value.data);
}
std::string owned(XpAppText value) {
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}

std::string_view view(XpAppText value) {
  return value.len ? std::string_view(reinterpret_cast<const char*>(value.data), value.len) : std::string_view();
}

const std::string& borrowed(XpAppNativeText value) {
  assert(value.native_string);
  const auto& result = *static_cast<const std::string*>(value.native_string);
  assert(result.size() == value.bytes.len && reinterpret_cast<const uint8_t*>(result.data()) == value.bytes.data);
  return result;
}

// Event formatting stays off the render call chain.
[[gnu::noinline]] void emitSession(XpAppText path, XpAppText document, const XpEpubSessionEvent* event) {
  char startTime[24], endTime[24], duration[16], startProgress[8], endProgress[8];
  snprintf(startTime, sizeof(startTime), "%lld", static_cast<long long>(event->start_time));
  snprintf(endTime, sizeof(endTime), "%lld", static_cast<long long>(event->end_time));
  snprintf(duration, sizeof(duration), "%lu", static_cast<unsigned long>(event->duration_seconds));
  snprintf(startProgress, sizeof(startProgress), "%u", event->start_progress);
  snprintf(endProgress, sizeof(endProgress), "%u", event->end_progress);
  const pluginevents::Var vars[] = {{"book", pathText(path)},         {"document", pathText(document)},
                                    {"start_time", startTime},        {"end_time", endTime},
                                    {"duration_seconds", duration},   {"start_progress_bp", startProgress},
                                    {"end_progress_bp", endProgress}, {"progress_scale", "10000"}};
  pluginevents::emit(pluginevents::Event::ReaderSession, vars, std::size(vars));
}

[[gnu::noinline]] void emitExit(XpAppText path, int32_t value) {
  char percent[8];
  snprintf(percent, sizeof(percent), "%d", static_cast<int>(value));
  const pluginevents::Var vars[] = {{"book", pathText(path)}, {"percent", percent}};
  pluginevents::emit(pluginevents::Event::ReaderExit, vars, std::size(vars));
}

#ifdef XP_APP_TEST
struct BookHandle {
  std::shared_ptr<Epub> book;

  bool closing = false;
  Epub& value() const {
    assert(!closing);
    return *book;
  }
  void bindExtractor() {
    assert(!closing);
    ImageBlock::setExtractor(book.get(), [](void* raw, const char* source, const char* destination) {
      return static_cast<Epub*>(raw)->extractItemToFile(source, destination);
    });
  }
};
BookHandle& book(void* handle) { return *static_cast<BookHandle*>(handle); }
#endif
MappedInputManager::Button inputButton(uint32_t index) {
  using Button = MappedInputManager::Button;
  constexpr Button buttons[] = {Button::Back, Button::Confirm, Button::Left, Button::Right,
                                Button::Up,   Button::Down,    Button::Power};
  assert(index < std::size(buttons));
  return buttons[index];
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

void readMargins(GfxRenderer& renderer, int32_t* out) {
  int margins[4]{};
  renderer.getOrientedViewableTRBL(&margins[0], &margins[1], &margins[2], &margins[3]);
  for (size_t i = 0; i < std::size(margins); ++i) out[i] = margins[i];
}

}  // namespace

extern GfxRenderer renderer;
extern "C" uint8_t xp_native_render_target(XpPageTarget* out, XpUiPaint* scenePaint) {
  renderer.pageTarget(*out, false);
  if (scenePaint) *scenePaint = renderer.uiPaint();
  return renderer.getFontCacheManager() != nullptr;
}

const XpEpubLeaves& rustEpubResources(GfxRenderer& renderer, MappedInputManager& input) {
  static GfxRenderer* const target = &renderer;
  static MappedInputManager* const mapped = &input;
  static const XpEpubLeaves leaves{
      .render_margins = [](int32_t* margins) { readMargins(*target, margins); },
      .render_geometry =
          [](XpEpubGeometry* out) {
            *out = {};
            out->width = target->getScreenWidth();
            out->height = target->getScreenHeight();
            out->status_height = UITheme::getInstance().getStatusBarHeight();
            out->progress_height = UITheme::getInstance().getProgressBarHeight();
            out->status_margin = UITheme::getInstance().getMetrics().statusBarVerticalMargin;
            out->ui_font_12 = UI_12_FONT_ID;
            out->title_mode = SETTINGS.statusBarSpec().titleMode;
            out->has_framebuffer = target->hasFrameBuffer();
            out->async_refresh = target->supportsAsyncRefresh();
            out->xteink_panel = gpio.isXteinkDevice() || BoardConfig::isX4Pro() || BoardConfig::isX4Classic() ||
                                BoardConfig::isEegoA4();
            out->a4 = BoardConfig::isEegoA4();
            out->has_font_cache = target->getFontCacheManager() != nullptr;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
            out->strip_enabled = true;
#endif
          },
      .render_restore_bw = [](uint8_t resync) { target->restoreBwBuffer(resync != 0); },
      .render_discard_bw = [] { target->discardStoredBwBuffer(); },
      .render_orientation = [](uint8_t orientation) { ReaderUtils::applyOrientation(*target, orientation); },
      .loan_count = [] { return target->frameBufferLoanCount(); },
#ifdef XP_APP_TEST
      .book_facade_open = [](void* owner, const XpEpubBookView* view, const XpEpubBookBorrow* borrow) -> void* {
        auto handle = makeUniqueNoThrow<BookHandle>();
        if (!handle) return nullptr;
        auto facade = makeUniqueNoThrow<Epub>(owner, *view, *borrow);
        if (!facade) return nullptr;
        handle->book = std::move(facade);
        ImageBlock::clearRenderFailures();
        handle->bindExtractor();
        return handle.release();
      },
      .book_facade_close =
          [](void* handle) {
            auto& facade = book(handle);
            assert(!facade.closing && facade.book.use_count() == 1);
            facade.closing = true;
            delete &facade;
          },
      .book_set_extractor =
          [](void* handle) {
            if (handle)
              book(handle).bindExtractor();
            else
              ImageBlock::setExtractor(nullptr, nullptr);
          },
#endif
// app-test keeps the legacy open/stream facade; shipping uses split reader primitives.
#ifdef XP_APP_TEST
      .protection_open = [](XpAppText path, XpAppTextSink error, void* context) -> void* {
        std::string message;
        auto protection = freeink::content::openProtectedBook(owned(path), message);
        if (!message.empty()) error(context, text(message));
        return protection.release();
      },
#else
      .protection_index = freeink::content::readerProtectionIndex,
#endif
      .protection_close = [](void* protection) { delete static_cast<freeink::content::ContentDecryptor*>(protection); },
      .protection_encrypted = [](void* protection, XpAppText path) -> uint8_t {
        return static_cast<freeink::content::ContentDecryptor*>(protection)->isEncrypted(owned(path));
      },
      .protection_size = [](void* protection, XpAppText path) -> uint32_t {
        return static_cast<freeink::content::ContentDecryptor*>(protection)->decryptedSize(owned(path));
      },
#ifdef XP_APP_TEST
      .protection_stream = [](void* protection, XpAppText path, XpBookSink sink, void* context) -> uint8_t {
        struct Output {
          XpBookSink sink;
          void* context;
        } output{sink, context};
        return static_cast<freeink::content::ContentDecryptor*>(protection)
            ->decryptToSink(
                owned(path),
                [](void* raw, const uint8_t* bytes, size_t size) {
                  const auto& out = *static_cast<Output*>(raw);
                  return out.sink(out.context, bytes, size) != 0;
                },
                &output);
      },
#else
      .protection_stream = freeink::content::readerProtectionStream,
#endif
      .file_seek = [](void* file, uint32_t offset) -> uint8_t { return static_cast<HalFile*>(file)->seek(offset); },
      .file_size = [](void* file) -> uint32_t { return static_cast<HalFile*>(file)->size(); },
      .path_remove_tree = [](XpAppText path) -> uint8_t { return Storage.removeDir(pathText(path)); },

      .storage_read_release = [](void* storage) { std::destroy_at(static_cast<String*>(storage)); },
      .paint_dither = [](int32_t x, int32_t y, int32_t width,
                         int32_t height) { target->fillRectDither(x, y, width, height, Color::LightGray); },
      .paint_pixel = [](int32_t x, int32_t y) { target->drawPixel(x, y, false); },
      .paint_status =
          [](const XpEpubStatus* value) {
            GUI.drawStatusBar(*target, value->book_progress, value->page, value->pages, owned(value->title), 0,
                              value->y_offset, true, value->bookmarked != 0, value->building != 0);
          },
#ifdef XP_APP_TEST
      .image_clear_failures = [] { ImageBlock::clearRenderFailures(); },
      .image_release_cache = [] { ImageBlock::releaseRenderCache(); },
#endif
#if defined(XP_SD_FONT_CATALOG) && !defined(XP_APP_TEST)
      .fonts_ensure_loaded =
          [](uint32_t action, XpSdSettings* settings, XpSdContinuation* token) {
            sdFontSystem.fontEffect(static_cast<XpSdAction>(action), *settings, *token, *target);
          },
#else
      .fonts_ensure_loaded = [] { sdFontSystem.ensureLoaded(*target); },
#endif
#ifdef XP_APP_TEST
      .fonts_release_caches =
          [] {
            if (auto* cache = target->getFontCacheManager()) cache->releaseSdFontCaches();
          },
      .fonts_prewarm_begin = [](XpEpubFontScan* storage) -> uint8_t { return physicalPrewarmBegin(*target, storage); },
      .fonts_prewarm_finish_scan = physicalPrewarmFinish,
      .fonts_prewarm_drop = physicalPrewarmDrop,
#endif
#ifdef XP_APP_TEST
      .fonts_take_corrupt_notice = []() -> uint8_t { return SdCardFont::takeCorruptGlyphNotice(); },
#else
      .fonts_take_corrupt_notice = xp_sd_font_take_corrupt_notice,
#endif
#ifndef XP_SD_FONT_CATALOG
      .font_sizes = [](XpAppText family, XpAppTextSink sink, void* context) -> uint8_t {
        // Families borrow NUL-terminated owner or settings storage.
        const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), pathText(family));
        return sink(context, {sizes.data(), sizes.size()});
      },
#endif
      .settings_snapshot =
          [](uint8_t resolveFont, XpEpubSettings* out) {
            const auto& settings = SETTINGS;
#if defined(XP_SD_FONT_CATALOG) && !defined(XP_APP_TEST)
            const int font = 0;
#else
            const int font = resolveFont ? settings.getReaderFontId() : 0;
#endif
            *out = {.font = font,
                    .orientation = settings.orientation,
                    .font_size = settings.fontPointSize,
                    .line_spacing = settings.lineSpacing,
                    .alignment = settings.paragraphAlignment,
                    .focus_reading = settings.focusReadingEnabled,
                    .screen_margin = settings.screenMargin,
                    .text_aa = settings.textAntiAliasing,
                    .embedded_style = settings.embeddedStyle,
                    .remove_read_recents = settings.removeReadBooksFromRecents,
                    .move_finished = settings.moveFinishedToReadFolder,
                    .menu_style = settings.readerMenuStyle,
                    .long_menu = settings.longPressMenuFunction,
                    .long_button = settings.longPressButtonBehavior,
                    .short_power = settings.shortPwrBtn,
                    .tilt = settings.tiltPageTurn,
                    .touch_controls = settings.touchReaderControls,
                    .page_gesture = settings.pageTurnGesture,
                    .previous_gesture = settings.previousPageGesture,
                    .show_menu = settings.showReaderMenu,
                    .back_to_browser = settings.backShortToFileBrowser,
                    .screen_inverted = settings.screenInverted,
                    .ble_enabled = settings.ble.enabled,
                    .dictionary_available = static_cast<uint8_t>(settings.dictionaryName[0] != '\0'),
                    .refresh_frequency = settings.refreshFrequency,
                    .font_family = text(settings.sdFontFamilyName)};
          },
      .settings_render_spec =
          [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
#ifdef XP_APP_TEST
            const auto spec = SETTINGS.readerRenderSpec(width, height);
            *out = {.font = spec.fontId,
                    .compression = spec.lineCompression,
                    .width = spec.viewportWidth,
                    .height = spec.viewportHeight,
                    .paragraph_spacing = spec.extraParagraphSpacing,
                    .paragraph_indent = spec.paragraphIndentSpaces,
                    .character_spacing = spec.characterSpacing,
                    .word_spacing_percent = spec.wordSpacingPercent,
                    .alignment = spec.paragraphAlignment,
                    .hyphenation = spec.hyphenationEnabled,
                    .embedded_style = spec.embeddedStyle,
                    .images = spec.imageRendering,
                    .focus_reading = spec.focusReadingEnabled};
#else
            const auto& settings = SETTINGS;
            *out = {.font = 0,
                    .compression = 0,
                    .width = width,
                    .height = height,
                    .paragraph_spacing = static_cast<uint8_t>(settings.extraParagraphSpacing != 0),
                    .paragraph_indent = settings.paragraphIndentSpaces,
                    .character_spacing = settings.getCharacterSpacing(),
                    .word_spacing_percent = settings.wordSpacing,
                    .alignment = settings.paragraphAlignment,
                    .hyphenation = static_cast<uint8_t>(settings.hyphenationEnabled != 0),
                    .embedded_style = static_cast<uint8_t>(settings.embeddedStyle != 0),
                    .images = settings.imageRendering,
                    .focus_reading = static_cast<uint8_t>(settings.focusReadingEnabled != 0)};
#endif
          },
      .settings_orientation = [](uint8_t value) { SETTINGS.orientation = value; },
      .settings_font_size = [](uint8_t value) { SETTINGS.fontPointSize = value; },
      .settings_line_spacing = [](uint8_t value) { SETTINGS.lineSpacing = value; },
      .settings_alignment = [](uint8_t value) { SETTINGS.paragraphAlignment = value; },
      .settings_focus_reading = [](uint8_t value) { SETTINGS.focusReadingEnabled = value; },
      .settings_inversion = [](uint8_t value) { SETTINGS.screenInverted = value; },
      .settings_ble_enabled = [](uint8_t value) { SETTINGS.ble.enabled = value; },
      .settings_save = [] { SETTINGS.saveToFile(); },
      .input_sources = []() -> const XpUiReaderInputServices* {
        static const auto services = ReaderUtils::inputSources(*mapped);
        return &services;
      },
      .input_facts =
          [](uint8_t home, XpEpubInputFacts* out) {
            *out = {};
            out->held_ms = mapped->getHeldTime(static_cast<HomeButtonAction>(home));
            for (uint8_t button = HalGPIO::BTN_BACK; button <= HalGPIO::BTN_POWER; ++button) {
              if (gpio.isPressed(button)) out->raw_held |= 1u << button;
            }
            out->raw_edges = gpio.wasAnyPressed() || gpio.wasAnyReleased();
            out->remote_click = mapped->hasRemoteClick();
            out->touch = mapped->hasTouch();
            out->board_touch = BoardConfig::hasTouch();
            out->home_action = home;
            out->slide_owns_light = slideSwitchOwnsLight();
          },
      .input_pressed = [](uint32_t button) -> uint8_t { return mapped->isPressed(inputButton(button)); },
      .input_screen_point = [](uint8_t kind, int32_t* x, int32_t* y) -> uint8_t {
        int hitX = 0, hitY = 0;
        switch (kind) {
          case 0:
            if (!mapped->wasScreenLongPress(hitX, hitY)) return 0;
            break;
          case 1:
            if (mapped->wasSwipe(&hitX, &hitY) == MappedInputManager::SwipeDir::None) return 0;
            break;
          default:
            return 0;
        }
        *x = hitX;
        *y = hitY;
        return 1;
      },
      .input_screen_tap = [](int32_t* x, int32_t* y) -> uint8_t {
        int hitX = 0, hitY = 0;
        if (!mapped->wasScreenTapped(hitX, hitY)) return 0;
        *x = hitX;
        *y = hitY;
        return 1;
      },
      .input_raw_contact = []() -> uint8_t { return gpio.rawInputActive(); },
      .render_busy = []() -> uint8_t { return RenderLock::peek(); },
      .file_open_read = [](XpAppText path, XpAppText origin) -> void* {
        auto file = makeUniqueNoThrow<HalFile>();
        if (!file || !Storage.openFileForRead(pathText(origin), pathText(path), *file)) return nullptr;
        return file.release();
      },
      .file_open_write = [](XpAppText path, XpAppText origin) -> void* {
        auto file = makeUniqueNoThrow<HalFile>();
        if (!file || !Storage.openFileForWrite(pathText(origin), pathText(path), *file)) return nullptr;
        return file.release();
      },
      .file_read = [](void* file, uint8_t* bytes, uint32_t length) -> int32_t {
        return static_cast<HalFile*>(file)->read(bytes, length);
      },
      .file_write = [](void* file, const uint8_t* bytes, uint32_t length) -> uint32_t {
        return static_cast<HalFile*>(file)->write(bytes, length);
      },
      .file_close = [](void* raw) -> uint8_t {
        auto* file = static_cast<HalFile*>(raw);
        if (!file) return 0;
        const bool ok = file->close();
        delete file;
        return ok;
      },
      .path_remove = [](XpAppText path) -> uint8_t { return Storage.remove(pathText(path)); },
      .path_rename = [](XpAppText from, XpAppText to) -> uint8_t {
        return Storage.rename(pathText(from), pathText(to));
      },
      .path_mkdir = [](XpAppText path) -> uint8_t { return Storage.mkdir(pathText(path)); },
      .native_path_hash = [](XpAppText path) -> uint64_t { return std::hash<std::string_view>{}(view(path)); },
      .heap_snapshot = [](XpEpubHeap* out) { *out = {ESP.getFreeHeap(), ESP.getMaxAllocHeap()}; },
#ifdef XP_APP_TEST
      .radio_status =
          [](XpEpubRadio* out, XpEpubPeer* peer) {
            const auto status = bleturner::status();
            *out = {};
            out->compiled_in = status.compiledIn;
            out->running = status.running;
            out->starting = status.starting;
            out->stopping = status.stopping;
            out->note = static_cast<uint8_t>(bleturner::linkNote());
            if (peer) {
              *peer = {};
              const auto linked = bleturner::linked();
              snprintf(reinterpret_cast<char*>(peer->name), sizeof(peer->name), "%s", linked.name);
              snprintf(reinterpret_cast<char*>(peer->addr), sizeof(peer->addr), "%s", linked.addr);
            }
          },
      .radio_before_build = []() -> uint8_t {
        return bleturner::beforeChapterBuild() != bleturner::BuildRelease::NotHeld;
      },
      .radio_settle_start = [] { bleturner::settleStart(); },
      .radio_holds_heap = []() -> uint8_t { return bleturner::holdsHeap(); },
      .radio_after_paint = [] { bleturner::afterPaint(); },
      .radio_switch_off = [] { bleturner::switchOff(); },
      .radio_request_connect = [] { bleturner::requestConnect(); },
#else
      .radio_peer =
          [](XpEpubPeer* peer) {
            *peer = {};
            const auto linked = bleturner::linked();
            snprintf(reinterpret_cast<char*>(peer->name), sizeof(peer->name), "%s", linked.name);
            snprintf(reinterpret_cast<char*>(peer->addr), sizeof(peer->addr), "%s", linked.addr);
          },
#endif
#ifdef XP_APP_TEST
      .trusted_now = [] { return trustedtime::trustedNow(); },
      .trusted_sync = [](uint32_t timeout) -> uint8_t { return trustedtime::syncNow(timeout); },
#else
      .time_services = trustedtime::readerServices,
      .time_start_sync = trustedtime::readerStartSync,
#endif
      .wifi_connected = []() -> uint8_t { return WiFi.status() == WL_CONNECTED; },
      .wifi_disconnect = [](uint8_t off) { WiFi.disconnect(off != 0); },
      .restart_reader = [] { silentRestartToReader(); },
      .child_text_settings = []() -> void* {
        return makeLegacyActivity<TextSettingsActivity>(*target, *mapped, &sdFontSystem.registry(),
                                                        TextSettingsActivity::Tab::Family)
            .release();
      },
      .child_selection = [](XpEpubOwnedPage* pages, uint8_t count, const XpEpubSelectionArgs* args) -> void* {
        return epubPage::selection(*target, *mapped, pages, count, *args);
      },

      .child_wifi = []() -> void* { return makeLegacyActivity<WifiSelectionActivity>(*target, *mapped).release(); },
      .child_sync = [](XpAppText path, const XpEpubPosition* current, const XpEpubSavedProgress* saved,
                       XpAppText chapter, uint8_t eventsOnly) -> void* {
        return makeLegacyActivity<KOReaderSyncActivity>(*target, *mapped, owned(path), position(*current),
                                                        SavedProgressPosition{owned(saved->xpath), saved->percentage},
                                                        owned(chapter), eventsOnly != 0)
            .release();
      },
      .popup_open = []() -> void* { return makeUniqueNoThrow<OptionPopup>().release(); },
      .popup_close = [](void* handle) { delete static_cast<OptionPopup*>(handle); },
      .popup_active = [](void* handle) -> uint8_t { return static_cast<OptionPopup*>(handle)->isActive(); },
      .popup_show =
          [](void* handle, XpAppText title, XpAppText message, uint32_t count, int32_t selected,
             uint8_t (*label)(void*, uint32_t, XpAppTextSink, void*), void* labelContext,
             void (*choose)(void*, int32_t), void* choiceContext) {
            assert(count <= INT32_MAX);
            static_cast<OptionPopup*>(handle)->show(
                pathText(title), pathText(message), static_cast<int>(count), selected,
                [label, labelContext](int index, std::string& output) {
                  const auto copy = [](void* context, XpAppText value) -> uint8_t {
                    auto& destination = *static_cast<std::string*>(context);
                    if (value.len)
                      destination.assign(reinterpret_cast<const char*>(value.data), value.len);
                    else
                      destination.clear();
                    return 1;
                  };
                  if (!label(labelContext, index, copy, &output)) output.clear();
                },
                [choose, choiceContext](int index) { choose(choiceContext, index); });
          },
      .popup_dismiss = [](void* handle) { static_cast<OptionPopup*>(handle)->dismiss(); },
      .popup_input =
          [](void* handle, XpEpubPopupInput* out) {
            auto& popup = *static_cast<OptionPopup*>(handle);
            *out = {};
            out->consumed = popup.handleInput(*mapped, [&] { out->changed = 1; });
            out->active = popup.isActive();
          },
      .popup_paint = [](void* handle) { static_cast<OptionPopup*>(handle)->render(*target); },
      .popup_process_render = [](void* handle) { static_cast<OptionPopup*>(handle)->processRender(*target, *mapped); },
      .end_open = []() -> void* { return makeUniqueNoThrow<EndOfBookOptions>(*target).release(); },
      .end_close = [](void* handle) { delete static_cast<EndOfBookOptions*>(handle); },
      .end_load = [](void* handle, XpAppText path) { static_cast<EndOfBookOptions*>(handle)->loadOnce(owned(path)); },
      .end_menu_active = [](void* handle) -> uint8_t { return static_cast<EndOfBookOptions*>(handle)->menuActive(); },
      .end_input =
          [](void* handle, void (*result)(void*, uint8_t, XpAppText), void* context) {
            std::string path;
            const auto action = static_cast<EndOfBookOptions*>(handle)->handleMenuInput(*mapped, &path);
            result(context, static_cast<uint8_t>(action), text(path));
          },
      .end_paint = [](void* handle) { static_cast<EndOfBookOptions*>(handle)->render(*target, *mapped); },

#ifdef XP_APP_TEST
      .recents_remove = [](XpAppText path) -> uint8_t { return RECENT_BOOKS.removeByPath(owned(path)); },
      .recents_add =
          [](const XpAppBook* value) {
            RECENT_BOOKS.addBook(view(value->path), view(value->title), view(value->author), view(value->cover));
          },
      .recents_update_path =
          [](XpAppText from, XpAppText to, XpAppText oldCache, XpAppText newCache) {
            RECENT_BOOKS.updatePath(owned(from), owned(to), owned(oldCache), owned(newCache));
          },
#else
      .store_rows = [](void* storage, XpStoreRows* out) { RECENT_BOOKS.readerRows(storage, out); },
      .store_rows_close = RecentBooksStore::releaseReaderRows,
      .store_move_row = [](size_t destination, size_t source) { RECENT_BOOKS.moveReaderRow(destination, source); },
#endif
      .state_open_path = [] { return text(APP_STATE.openEpubPath); },
      .state_set_open_path =
          [](XpAppText path) { APP_STATE.openEpubPath.assign(reinterpret_cast<const char*>(path.data), path.len); },
      .state_reset_reader_count = [] { APP_STATE.readerActivityLoadCount = 0; },
#ifdef XP_APP_TEST
      .state_save = [] { APP_STATE.saveToFile(); },
#else
      .store_document =
          [](uint8_t operation, void* storage, XpStoreJsonOps* json, XpStorePersistOps* file, XpStoreState* state) {
            enum class Operation : uint8_t { State, Recent, Close };
            switch (static_cast<Operation>(operation)) {
              case Operation::State:
                APP_STATE.beginReaderSave(CrossPointState::getFilePath(), storage, json, file);
                *state = APP_STATE.readerView();
                break;
              case Operation::Recent:
                RECENT_BOOKS.beginReaderSave(RecentBooksStore::getFilePath(), storage, json, file);
                break;
              case Operation::Close:
                PersistableStoreBase::endReaderSave(storage);
                break;
            }
          },
#endif
      .has_sync_credentials = []() -> uint8_t { return KOREADER_STORE.hasCredentials(); },
#ifdef XP_APP_TEST
      .has_exit_subscriber = []() -> uint8_t { return pluginevents::anySubscriber(pluginevents::Event::ReaderExit); },
      .has_session_subscriber = []() -> uint8_t {
        return pluginevents::anySubscriber(pluginevents::Event::ReaderSession);
      },
#else
      .subscribers = pluginevents::subscriptionView,
#endif
      .document_id = [](XpAppText path, XpAppTextSink sink, void* context) -> uint8_t {
        const auto document = KOReaderDocumentId::calculate(owned(path));
        return sink(context, text(document));
      },
      .emit_open =
          [](XpAppText path) {
            const pluginevents::Var vars[] = {{"book", pathText(path)}};
            pluginevents::emit(pluginevents::Event::ReaderOpen, vars, std::size(vars));
          },
      .emit_exit = emitExit,
      .emit_session = emitSession,
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
#ifdef XP_APP_TEST
      .font_text_advance = [](XpAppText value, int32_t font, uint8_t style, int8_t spacing) -> int32_t {
        return target->getTextAdvanceX(font, pathText(value), static_cast<EpdFontFamily::Style>(style), spacing);
      },
#endif
      .log_enabled = [](uint8_t severity) -> uint8_t {
#ifdef ENABLE_SERIAL_LOG
        switch (severity) {
          case 0:
            return LOG_LEVEL >= 2;
          case 1:
            return LOG_LEVEL >= 0;
          case 2:
            return LOG_LEVEL >= 1;
          default:
            return 0;
        }
#else
        static_cast<void>(severity);
        return 0;
#endif
      },
      .input_snapshot = [](uint8_t home,
                           XpUiInput* out) { *out = rustInputSnapshot(*mapped, static_cast<HomeButtonAction>(home)); },
      .cache_lock = [] { BookMetadataCache::lockReadCursor(); },
      .cache_unlock = [] { BookMetadataCache::unlockReadCursor(); },
      .scratch_claim = [](uint32_t min_len) { return buildscratch::claim(min_len); },
      .scratch_release = [](const uint8_t* bytes) { buildscratch::release(bytes); },
      .inflate_oom_log = [](XpAppText allocation,
                            uint32_t bytes) { InflateStream::logAllocationFailure(pathText(allocation), bytes); },
      .build_metrics = []() -> const XpParaMetrics* { return &sectionMetrics; },
#ifdef XP_APP_TEST
      .build_allocator = paragraphAllocator,
#else
      .build_allocator = paragraphPhysicalAllocator,
#endif
#if defined(XP_APP_TEST) || !defined(XP_SD_FONT_CATALOG)
      .font_variant = [](int32_t font, float scale) -> int32_t {
#if defined(XP_SD_FONT_CATALOG) && !defined(XP_APP_TEST)
        return target->resolveNonSdFontVariant(font, scale);
#else
        return target->resolveFontVariant(font, scale);
#endif
      },
#endif
#ifdef XP_APP_TEST
      .font_line_height = [](int32_t font, float compression) -> int32_t {
        return target->getLineHeight(font, compression);
      },
#endif
#if defined(XP_APP_TEST) || !defined(XP_SD_FONT_CATALOG)
      .font_ready = [](int32_t font, int32_t fallback) -> int32_t {
#if defined(XP_SD_FONT_CATALOG) && !defined(XP_APP_TEST)
        return font == fallback || target->ensureNonSdFontLoaded(font) ? font : fallback;
#else
        return font == fallback || target->ensureFontLoaded(font) ? font : fallback;
#endif
      },
#endif
#ifdef XP_APP_TEST
      .font_codepoint = [](int32_t font, uint32_t codepoint, uint8_t style, int32_t* advance, int32_t* top) -> uint8_t {
        int32_t measuredAdvance = 0;
        int measuredTop = 0;
        const bool found = target->getCodepointMetrics(font, codepoint, static_cast<EpdFontFamily::Style>(style),
                                                       measuredAdvance, measuredTop);
        *advance = measuredAdvance;
        *top = measuredTop;
        return found;
      },
#endif
#ifdef XP_APP_TEST
      .hyphen_language = [](XpAppText language) -> int32_t {
        Hyphenator::setPreferredLanguage(owned(language));
        return Hyphenator::preferredLanguageId();
      },
#else
      .hyphen_language = Hyphenator::setPreferredLanguageId,
#endif
      .file_position = [](void* file) -> uint32_t { return static_cast<HalFile*>(file)->position(); },
      .file_flush = [](void* file) { static_cast<HalFile*>(file)->flush(); },
      .image_supported = [](XpAppText path) -> uint8_t {
        return ImageDecoderFactory::isFormatSupported(pathText(path));
      },
      .image_dimensions = [](XpAppText path, uint16_t* width, uint16_t* height) -> uint8_t {
        auto* decoder = ImageDecoderFactory::getDecoder(pathText(path));
        if (!decoder) return 0;
        ImageDimensions dimensions{static_cast<int16_t>(*width), static_cast<int16_t>(*height)};
        const bool found = decoder->getDimensions(pathText(path), dimensions);
        *width = dimensions.width;
        *height = dimensions.height;
        return found ? 2 : 1;
      },
      .framebuffer_lend = []() -> uint8_t {
        if (!target->hasFrameBuffer()) return 0;
        target->releaseFrameBufferForBuild();
        return 1;
      },
      .framebuffer_restore =
          [](uint8_t owned) {
            if (owned && !target->restoreFrameBufferAfterBuild()) {
              LOG_ERR("GFX", "Framebuffer restore failed - restarting");
              ESP.restart();
            }
          },
#ifdef XP_APP_TEST
      .image_needs_decode = epubPage::imageNeedsDecode,
      .font_ascent = [](int32_t font) -> int32_t { return epubPage::fontAscent(*target, font); },
#endif
      .render_physical_query = [](uint8_t query) -> uint32_t { return physicalRenderQuery(*target, query); },
      .render_physical_operation = [](const XpEpubPhysicalOp* op) -> uint32_t {
        return physicalRenderOperation(*target, *op);
      },
      .image_read_window = physicalImageWindow,
      .display_source = physicalDisplaySource,
      .framebuffer_view = [](uint32_t* bytes) -> uint8_t* { return physicalFramebuffer(*target, bytes); },
      .child_list_theme = [](XpUiReaderTheme* theme, XpUiReaderThemeFacts* facts,
                             uint8_t toolbar) { sampleReaderChildTheme(*target, *theme, *facts, toolbar != 0); },
      .child_utility_layout = [](XpUiUtilityLayout* layout) -> uint8_t {
        return sampleReaderUtilityLayout(*target, *mapped, *layout);
      },
      .child_header = [](XpUiRect bounds, XpAppText title,
                         uint8_t list) { drawReaderHeader(*target, bounds, title, list != 0); },
      .child_utility_footer = [](XpUiRect clip,
                                 const XpAppText* labels) { drawReaderUtilityFooter(*target, *mapped, clip, labels); },
      .child_qr_draw =
          [](XpUiRect bounds, XpAppText payload) {
            QrUtils::drawQrBytes(*target, Rect(bounds.x, bounds.y, bounds.w, bounds.h), payload.data, payload.len);
          },
      .child_toolbar_icon = [](uint32_t icon, XpUiRect bounds,
                               XpUiRect clip) { drawReaderToolbarIcon(*target, icon, bounds, clip); },
#ifdef XP_APP_TEST
      .child_clear_fonts =
          [] {
            if (auto* cache = target->getFontCacheManager()) cache->clearCache();
          },
      .child_prewarm_rows = [](int32_t font, const void* context, XpEpubChildRowText row,
                               uint32_t count) { prewarmReaderRows(*target, font, context, row, count); },
#endif
      .child_lookup_init = readerLookup::init,
      .child_lookup_drop = readerLookup::drop,
      .child_lookup_open = readerLookup::open,
      .child_lookup_index = readerLookup::index,
      .child_lookup_find = readerLookup::find,
      .child_lookup_discard = readerLookup::discard,
      .child_definition_payload_lookup = readerLookup::payloadLookup,
      .child_definition_payload_plain = DefinitionPayload::constructPlain,
      .child_definition_payload_native = DefinitionPayload::constructNative,
      .child_definition_payload_drop =
          [](XpDefinitionPayloadStorage* storage) { std::destroy_at(&DefinitionPayload::get(storage)); },
      .child_definition_heap =
          [](XpEpubHeap* out) {
            const auto heap = HalMemory::getDefaultHeap();
            *out = {heap.freeBytes, heap.largestBlockBytes};
          },
      .child_definition_payload_prepare = [](XpDefinitionPayloadStorage* storage, uint8_t slot, uint8_t html,
                                             uint8_t plain, XpUiRect body, XpDefinitionPayloadView* out) -> uint8_t {
        auto& payload = DefinitionPayload::get(storage);
        auto& value = payload.slot(slot);
        if (!payload.prepareDefinition(*target, value, html, plain, body)) {
          value.lines.clear();
          return 0;
        }
        *out = {value.totalPages, static_cast<uint8_t>(!value.pages.empty()), {0, 0, 0}};
        return 1;
      },
      .child_definition_payload_release =
          [](XpDefinitionPayloadStorage* storage, uint8_t slot, uint8_t keepHeadword) {
            auto& payload = DefinitionPayload::get(storage).slot(slot);
            payload.releasePayload();
            if (!keepHeadword) std::string{}.swap(payload.headword);
          },
      .child_definition_payload_find = readerLookup::payloadFind,
      .child_definition_payload_commit =
          [](XpDefinitionPayloadStorage* storage) {
            auto& payload = DefinitionPayload::get(storage);
            payload.slots[0] = std::move(payload.slots[1]);
          },
      .child_definition_selection_build = [](XpDefinitionPayloadStorage* storage, int32_t page) -> uint8_t {
        return DefinitionPayload::get(storage).buildPlainSelectionPages(*target, page);
      },
      .child_definition_selection_child = [](XpDefinitionPayloadStorage* storage,
                                             const XpDefinitionSelectionRequest* request, XpUiRect body) -> void* {
        return DefinitionPayload::get(storage).selectionChild(*target, *mapped, *request, body);
      },
      .child_definition_selection_release =
          [](XpDefinitionPayloadStorage* storage) { DefinitionPayload::get(storage).selectionPages.clear(); },
      .child_definition_layout = [](XpUiDefinitionLayout* out, int32_t x, int32_t y) -> uint8_t {
        const bool headerBackHit = (x != -1 || y != -1) && HeaderBackTapTarget::contains(x, y);
        *out = dictionaryUiDefinitionLayout(*target);
        return headerBackHit;
      },
      .child_definition_draw_header =
          [](const XpDefinitionPayloadStorage* storage, XpUiRect bounds, int32_t page, int32_t pages) {
            DefinitionPayload::get(storage).drawHeader(*target, bounds, page, pages);
          },
      .child_definition_draw_body =
          [](const XpDefinitionPayloadStorage* storage, int32_t font, int32_t x, int32_t y, int32_t page) {
            DefinitionPayload::get(storage).drawBody(*target, font, x, y, page);
          },
      .child_directional_hints =
          [](const XpAppText* labels) { readerLookup::directionalHints(*target, *mapped, labels); },
      .child_dictionary_popup_layout = [](XpUiDictionaryPopup* out) { readerLookup::popupLayout(*target, out); },
      .child_wait_paint = [] { activityManager.requestUpdateAndWait(); },
      .clipping_physical = &CLIPPING_PHYSICAL,
      .child_list_help =
          [](XpUiRect bounds, XpAppText text) {
            GUI.drawHelpText(*target, Rect(bounds.x, bounds.y, bounds.w, bounds.h), pathText(text));
          },
      .child_list_icon =
          [](uint8_t kind, XpUiRect bounds, XpUiRect clip, uint8_t paint) {
            namespace fui = freeink::ui;
            const auto bitmap = kind == 1 ? fui::bitmapFromIcon(icon_trash_2_24) : listIconFor(UIIcon::Bookmark, 32);
            const auto previous = target->getClipRect();
            target->setClipRect(clip.x, clip.y, clip.w, clip.h);
            auto canvas = makeUiTarget(*target);
            canvas.bitmap({bounds.x, bounds.y, bounds.w, bounds.h}, bitmap, fui::BitmapMode::Contain,
                          fui::Paint::solid(paint == 1 ? fui::Color::White : fui::Color::Black));
            target->setClipRect(previous[0], previous[1], previous[2], previous[3]);
          },
      .local_calendar = [](XpEpubCalendar* out) -> uint8_t {
        struct tm local;
        if (!halClock.localTime(local)) return 0;
        *out = {local.tm_year, local.tm_mon, local.tm_mday, local.tm_wday, local.tm_hour, local.tm_min};
        return 1;
      },
      .storage_read_file = [](XpAppText path, void* storage) -> XpAppText {
        static_assert(sizeof(String) <= 6 * sizeof(uintptr_t) && alignof(String) <= alignof(uintptr_t));
        auto* bytes = std::construct_at(static_cast<String*>(storage), Storage.readFile(pathText(path)));
        return {reinterpret_cast<const uint8_t*>(bytes->c_str()), bytes->length()};
      },
      .storage_write_file_atomic = [](XpAppText path, const uint8_t* bytes, uint32_t size) -> uint8_t {
        return Storage.writeFileAtomic(pathText(path), bytes, size);
      },
#ifndef XP_APP_TEST
      .protection_finish = freeink::content::readerProtectionFinish,
      .key_platform = bookkey::readerPlatform,
#endif
  };
  if (!sectionMetrics.context) {
#ifndef XP_APP_TEST
    sectionMetrics.context = const_cast<XpLoadedFonts*>(target->getFontMap().raw());
#else
    sectionMetrics.context = target;
#endif
  }
  return leaves;
}
