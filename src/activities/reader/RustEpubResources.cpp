#include "RustEpubResources.h"

#include <BidiUtils.h>
#include <BlePageTurner.h>
#include <BoardConfig.h>
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
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <InflateStream.h>
#include <KOReaderDocumentId.h>
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
#include <string_view>
#include <vector>

#include "BookmarkEntry.h"
#include "ClipSelectionActivity.h"
#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryWordSelectActivity.h"
#include "EndOfBookOptions.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderClippingListActivity.h"
#include "EpubReaderFootnoteSelectActivity.h"
#include "EpubReaderMenuActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "QrDisplayActivity.h"
#include "ReaderFontSizes.h"
#include "ReaderToolbarUi.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "RustEpubPage.h"
#include "SilentRestart.h"
#include "activities/OwnedLegacyActivity.h"
#include "activities/RenderLock.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "clippings/ClippingsManager.h"
#include "components/OptionPopup.h"
#include "components/RustInputSnapshot.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookmarkFile.h"
#include "util/HomeButtonInput.h"
#include "util/PluginEvents.h"
#include "util/SlideSwitch.h"
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
#include "ReaderStripBuffers.h"
#include "ReaderStripPainter.h"
#endif

const XpParaAllocator* paragraphAllocator();

namespace {
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
XpParaMetrics sectionMetrics{nullptr,
                             nullptr,  // Layout tracing is installed only by the host instrumenter.
#ifdef XP_NATIVE_TEXT_MEASUREMENT
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             prepareText,
                             nullptr,
                             nullptr,
#else
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
                             [](void* ctx, XpNativeProvider* out) -> uint8_t {
                               *out = sectionRenderer(ctx).textProvider();
                               return true;
                             },
#else
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
// Native toolbar events cross the typed routed-result leaf.
static_assert(static_cast<uint8_t>(ReaderToolbarUi::Event::None) == 0);
static_assert(static_cast<uint8_t>(ReaderToolbarUi::Event::Dismiss) == 1);
static_assert(static_cast<uint8_t>(ReaderToolbarUi::Event::Tool) == 2);
static_assert(static_cast<uint8_t>(ReaderToolbarUi::Event::PrevChapter) == 3);
static_assert(static_cast<uint8_t>(ReaderToolbarUi::Event::NextChapter) == 4);
static_assert(static_cast<uint8_t>(ReaderToolbarUi::Event::Scrub) == 5);
static_assert(static_cast<uint8_t>(ReaderToolbarUi::Event::Row) == 6);
// Raw mapped directions are translated to shared XpUiSwipe by ReaderUtils.
static_assert(static_cast<uint8_t>(MappedInputManager::SwipeDir::None) == 0);
static_assert(static_cast<uint8_t>(MappedInputManager::SwipeDir::Left) == 1);
static_assert(static_cast<uint8_t>(MappedInputManager::SwipeDir::Right) == 2);
static_assert(static_cast<uint8_t>(MappedInputManager::SwipeDir::Up) == 3);
static_assert(static_cast<uint8_t>(MappedInputManager::SwipeDir::Down) == 4);
// Native child results carry these MenuAction values to Rust.
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER) == 0);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::FOOTNOTES) == 1);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS) == 2);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::NIGHT_MODE) == 3);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::FRONTLIGHT) == 4);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::BLUETOOTH) == 5);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT) == 6);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN) == 7);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN) == 8);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::BOOKMARKS) == 9);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::SAVE_CLIPPING) == 10);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::VIEW_CLIPPINGS) == 11);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK) == 12);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::SCREENSHOT) == 13);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::DISPLAY_QR) == 14);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::GO_HOME) == 15);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::SYNC) == 16);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::DELETE_CACHE) == 17);
static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::DICTIONARY) == 18);
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
static_assert(ReaderStripBuffers::ROWS == 80 && ReaderStripBuffers::MAX_ELEMENTS == 512);
#endif

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
struct NativeRenderFrame {
  GfxRenderer& renderer;
  XpEpubHighlight highlight;
  void* highlightContext;
  XpEpubPageView pageView;
  Page page;
  int font, left, top;
  bool images = false;
  struct ImageGuard {
    ~ImageGuard() { ImageBlock::releaseRenderCache(); }
  } imageGuard;
  std::optional<FontCacheManager::PrewarmScope> prewarm;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  struct StripFrame {
    GfxRenderer& renderer;
    ReaderStripBuffers& buffers;
    ReaderStripPainter painter;
    XpPagePlan plan{};
    XpDisplaySource source{};
    bool active = false;
    ~StripFrame() {
      if (active) display.setFrameSource(nullptr);
      buffers.setSourceLoanActive(false);
      renderer.usePagePlan(nullptr, nullptr, 0);
      ImageBlock::setRenderReadWindow(nullptr, 0);
    }
  } strips;
#endif
  std::unique_ptr<uint8_t[]> lsb, msb, scratch;
  uint8_t* borrowedScratch = nullptr;
  unsigned long times[10]{};
  NativeRenderFrame(GfxRenderer& renderer, const XpEpubPageView& view, int font, int left, int top, int bottom,
                    XpEpubHighlight highlight, void* context
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
                    ,
                    ReaderStripBuffers& buffers
#endif
                    )
      : renderer(renderer),
        highlight(highlight),
        highlightContext(context),
        pageView(view),
        page(Page::Borrowed{}, view.arena, view.size, view.visible_offset),
        font(font),
        left(left),
        top(top)
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        ,
        strips{renderer,
               buffers,
               {renderer, page, font, left, top, renderer.getScreenHeight() - bottom - 8, display.isInverted(), this,
                [](void* raw, const Page&, int font, int left, int top) {
                  auto& frame = *static_cast<NativeRenderFrame*>(raw);
                  frame.highlight(frame.highlightContext, &frame.pageView, font, left, top);
                }}}
#endif
  {
    (void)bottom;
  }
  uint8_t* buffer(uint8_t id) {
    switch (static_cast<XpUiReaderRenderBuffer>(id)) {
      case XP_UI_READER_BUFFER_LSB:
        return lsb.get();
      case XP_UI_READER_BUFFER_MSB:
        return msb.get();
      case XP_UI_READER_BUFFER_SCRATCH:
        return borrowedScratch ? borrowedScratch : scratch.get();
    }
    return nullptr;
  }
  uint32_t read(uint8_t query) {
    switch (static_cast<XpUiReaderRenderQuery>(query)) {
      case XP_UI_READER_RENDER_NOW:
        return millis();
      case XP_UI_READER_RENDER_HAS_IMAGES:
        images = page.hasImages();
        return images;
      case XP_UI_READER_RENDER_IMAGES_NEED_DECODE:
        return page.hasImagesNeedingDecode();
      case XP_UI_READER_RENDER_TAKE_FORCED:
      case XP_UI_READER_RENDER_COUNTER:
        assert(false);  // Intercepted by the Rust session.
        return 0;
      case XP_UI_READER_RENDER_FREQUENCY:
        return SETTINGS.getRefreshFrequency();
      case XP_UI_READER_RENDER_TEXT_AA:
        return SETTINGS.textAntiAliasing;
      case XP_UI_READER_RENDER_IS_A4:
        return BoardConfig::isEegoA4();
      case XP_UI_READER_RENDER_IS_BW:
        return renderer.getRenderMode() == GfxRenderer::BW;
      case XP_UI_READER_RENDER_IS_X3:
        return gpio.deviceIsX3();
      case XP_UI_READER_RENDER_IS_UC8279:
        return display.getController() == HalDisplay::Controller::UC8279;
      case XP_UI_READER_RENDER_ABSOLUTE_SUPPORTED:
        return renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
      case XP_UI_READER_RENDER_OVERLAY_CAPS:
      case XP_UI_READER_RENDER_ABSOLUTE_CAPS: {
        const auto caps = renderer.grayscaleCapabilities(query == XP_UI_READER_RENDER_ABSOLUTE_CAPS
                                                             ? HalDisplay::GrayscaleMode::Absolute
                                                             : HalDisplay::GrayscaleMode::Overlay);
        return (caps.stripUploads ? 1u : 0u) | (caps.base == HalDisplay::GrayscaleBase::Combined ? 2u : 0u) |
               (caps.asyncBase ? 4u : 0u);
      }
      case XP_UI_READER_RENDER_STRIP_BOARD:
        return display.getController() == HalDisplay::Controller::UC8253 ||
               (BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4 &&
                display.getController() == HalDisplay::Controller::SSD1677);
      case XP_UI_READER_RENDER_ELEMENTS:
        return page.elementCount();
      case XP_UI_READER_RENDER_WIDTH_BYTES:
        return renderer.getDisplayWidthBytes();
      case XP_UI_READER_RENDER_HEIGHT:
        return renderer.getDisplayHeight();
      case XP_UI_READER_RENDER_FREE_HEAP:
        return ESP.getFreeHeap();
      case XP_UI_READER_RENDER_MAX_BLOCK:
        return ESP.getMaxAllocHeap();
    }
    return 0;
  }
  [[gnu::noinline]] void log(uint32_t id) const {  // Keep log locals off the nested page-drawing stack.
    const auto* t = times;
    switch (static_cast<XpUiReaderRenderLog>(id)) {
      case XP_UI_READER_RENDER_STRIP_OOM:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        LOG_ERR(
            "ERS", "OOM: reader strips (%u bytes); using framebuffer",
            static_cast<unsigned>(ReaderStripBuffers::SCRATCH_BYTES + ReaderStripBuffers::COMMAND_BYTES +
                                  sizeof(XpPageRowBounds) * ReaderStripBuffers::MAX_ELEMENTS + (images ? 4096 : 0)));
#endif
        break;
      case XP_UI_READER_RENDER_ABSOLUTE_FAILED:
        LOG_ERR("ERS", "Could not start absolute image page; displaying B/W");
        break;
      case XP_UI_READER_RENDER_ABSOLUTE_QUALITY:
        LOG_DBG("ERS", "UC8279 image page: absolute quality waveform");
        break;
      case XP_UI_READER_RENDER_SCRATCH_OOM:
        LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); skipping AA this page",
                renderer.getDisplayWidthBytes() * 80);
        break;
      case XP_UI_READER_RENDER_STORE_FAILED:
        LOG_ERR("ERS", "Failed to store BW buffer for grayscale render; skipping grayscale this page");
        break;
      case XP_UI_READER_RENDER_TIMING_ASYNC:
        LOG_DBG("ERS",
                "Page render (tiled async): prewarm=%lums bw_render=%lums display=%lums gray_render=%lums "
                "wait=%lums gray_write=%lums gray_display=%lums cleanup=%lums total=%lums (planes buffered: %d)",
                t[1] - t[0], t[2] - t[1], t[3] - t[2], t[4] - t[3], t[5] - t[4], t[6] - t[5], t[7] - t[6], t[8] - t[7],
                t[8] - t[0], msb ? 2 : 1);
        break;
      case XP_UI_READER_RENDER_TIMING_TILED:
        LOG_DBG("ERS",
                "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums gray_lsb=%lums "
                "gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums",
                t[1] - t[0], t[2] - t[1], t[3] - t[2], t[4] - t[3], t[5] - t[4], t[6] - t[5], t[7] - t[6], t[8] - t[0]);
        break;
      case XP_UI_READER_RENDER_TIMING_GRAY:
        LOG_DBG("ERS",
                "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
                "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
                t[1] - t[0], t[2] - t[1], t[3] - t[2], t[4] - t[3], t[5] - t[4], t[6] - t[5], t[7] - t[6], t[8] - t[7],
                t[9] - t[0]);
        break;
      case XP_UI_READER_RENDER_TIMING_BW:
        LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", t[1] - t[0], t[2] - t[1],
                t[3] - t[2], t[4] - t[0]);
        break;
    }
  }
  uint32_t execute(const XpUiReaderRenderOp& op) {
    const auto refresh = op.mode == XP_UI_HALF ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
    switch (static_cast<XpUiReaderRenderOperation>(op.kind)) {
      case XP_UI_READER_RENDER_IMAGE_RESET:
        ImageBlock::clearRenderFailures();
        break;
      case XP_UI_READER_RENDER_PREWARM_BEGIN:
        prewarm.emplace(*renderer.getFontCacheManager());
        break;
      case XP_UI_READER_RENDER_PAGE:
        page.render(renderer, font, left, top);
        break;
      case XP_UI_READER_RENDER_STATUS:
        assert(false);  // Intercepted by the Rust session.
        break;
      case XP_UI_READER_RENDER_PREWARM_END:
        prewarm->endScanAndPrewarm();
        break;
      case XP_UI_READER_RENDER_HIGHLIGHTS:
        highlight(highlightContext, &pageView, font, left, top);
        break;
      case XP_UI_READER_RENDER_IMAGES:
        page.renderImages(renderer, font, left, top);
        break;
      case XP_UI_READER_RENDER_PLACEHOLDERS:
        page.renderWithImagePlaceholders(renderer, font, left, top);
        break;
      case XP_UI_READER_RENDER_CLEAR:
        renderer.clearScreen(op.value);
        break;
      case XP_UI_READER_RENDER_DISPLAY:
        renderer.displayBuffer(refresh);
        break;
      case XP_UI_READER_RENDER_DISPLAY_ASYNC:
        renderer.displayBufferAsync(refresh);
        break;
      case XP_UI_READER_RENDER_GRAY_BASE:
        renderer.displayGrayscaleBase(refresh);
        break;
      case XP_UI_READER_RENDER_ABSOLUTE_BASE:
        return renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, refresh);
      case XP_UI_READER_RENDER_PRECONDITION:
        renderer.preconditionGrayscale();
        break;
      case XP_UI_READER_RENDER_SET_COUNTER:
        assert(false);  // Intercepted by the Rust session.
        break;
      case XP_UI_READER_RENDER_PREPARE_STRIPS:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        return strips.buffers.prepare(renderer.getDisplayWidthBytes(), op.value != 0);
#else
        return false;
#endif
      case XP_UI_READER_RENDER_IMAGE_WINDOW:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        ImageBlock::setRenderReadWindow(strips.buffers.imageWindow.get(), strips.buffers.imageWindow ? 4096 : 0);
#endif
        break;
      case XP_UI_READER_RENDER_RECORD_BEGIN:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        strips.plan = {strips.buffers.commands.get(), ReaderStripBuffers::COMMAND_BYTES, 0, 0};
        renderer.usePagePlan(&strips.plan, strips.buffers.bounds.get(), page.elementCount(), true);
#endif
        break;
      case XP_UI_READER_RENDER_RECORD_END:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        renderer.usePagePlan(strips.plan.valid ? &strips.plan : nullptr, strips.buffers.bounds.get(),
                             page.elementCount());
        return strips.plan.valid;
#else
        return false;
#endif
      case XP_UI_READER_RENDER_SET_SOURCE:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        strips.source = {sizeof(XpDisplaySource),      &strips.painter,
                         strips.buffers.scratch.get(), op.value,
                         ReaderStripBuffers::ROWS,     renderer.getDisplayWidth(),
                         &ReaderStripPainter::paint};
        strips.active = display.setFrameSource(&strips.source);
        strips.buffers.setSourceLoanActive(strips.active);
        return strips.active;
#else
        return false;
#endif
      case XP_UI_READER_RENDER_CLEAR_PLAN:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        renderer.usePagePlan(nullptr, nullptr, 0);
#endif
        break;
      case XP_UI_READER_RENDER_CLEAR_WINDOW:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        ImageBlock::setRenderReadWindow(nullptr, 0);
#endif
        break;
      case XP_UI_READER_RENDER_RELEASE_STRIPS:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        strips.buffers.releaseIdle();
#endif
        break;
      case XP_UI_READER_RENDER_ALLOCATE:
        switch (static_cast<XpUiReaderRenderBuffer>(op.buffer)) {
          case XP_UI_READER_BUFFER_LSB:
            lsb = makeUniqueNoThrow<uint8_t[]>(op.value);
            return lsb != nullptr;
          case XP_UI_READER_BUFFER_MSB:
            msb = makeUniqueNoThrow<uint8_t[]>(op.value);
            return msb != nullptr;
          case XP_UI_READER_BUFFER_SCRATCH:
            scratch = makeUniqueNoThrow<uint8_t[]>(op.value);
            return scratch != nullptr;
        }
        return false;
      case XP_UI_READER_RENDER_BORROW_SCRATCH:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        borrowedScratch = strips.buffers.scratch.get();
        return borrowedScratch != nullptr;
#else
        return false;
#endif
      case XP_UI_READER_RENDER_MODE:
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
      case XP_UI_READER_RENDER_STRIP_BEGIN:
        renderer.beginStripTarget(buffer(op.buffer) + op.offset, op.y, op.rows);
        break;
      case XP_UI_READER_RENDER_STRIP_END:
        renderer.endStripTarget();
        break;
      case XP_UI_READER_RENDER_WAIT:
        renderer.waitRefreshComplete();
        break;
      case XP_UI_READER_RENDER_WRITE_PLANE:
        renderer.writeGrayscalePlaneStrip(op.mode == 1, buffer(op.buffer), op.y, op.rows);
        break;
      case XP_UI_READER_RENDER_GRAY_DISPLAY:
        renderer.displayGrayBuffer();
        break;
      case XP_UI_READER_RENDER_CLEANUP:
        renderer.cleanupGrayscaleWithFrameBuffer();
        break;
      case XP_UI_READER_RENDER_STORE_BW:
        return renderer.storeBwBuffer();
      case XP_UI_READER_RENDER_RESTORE_BW:
        renderer.restoreBwBuffer();
        break;
      case XP_UI_READER_RENDER_COPY_LSB:
        renderer.copyGrayscaleLsbBuffers();
        break;
      case XP_UI_READER_RENDER_COPY_MSB:
        renderer.copyGrayscaleMsbBuffers();
        break;
      case XP_UI_READER_RENDER_LOG:
        log(op.value);
        break;
      case XP_UI_READER_RENDER_MARK:
        times[op.value] = millis();
        break;
    }
    return 0;
  }
};

struct NativeResources {
  GfxRenderer& renderer;
  XpUiReaderRenderServices renderServices;
  std::optional<FontCacheManager::PrewarmScope> prewarm;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  ReaderStripBuffers strips;
#endif
  // Page-bound physical scratch replaces the former renderContents stack context.
  std::optional<NativeRenderFrame> frame;
  explicit NativeResources(GfxRenderer& renderer)
      : renderer(renderer),
        renderServices{
            this, [](void* raw, uint8_t query) -> uint32_t { return static_cast<NativeResources*>(raw)->read(query); },
            [](void* raw, const XpUiReaderRenderOp* op) -> uint32_t {
              return static_cast<NativeResources*>(raw)->execute(*op);
            }} {}
  uint32_t read(uint8_t query) {
    switch (static_cast<XpUiReaderRenderQuery>(query)) {
      case XP_UI_READER_RENDER_NOW:
        return millis();
      case XP_UI_READER_RENDER_IS_A4:
        return BoardConfig::isEegoA4();
      case XP_UI_READER_RENDER_IS_BW:
        return renderer.getRenderMode() == GfxRenderer::BW;
      case XP_UI_READER_RENDER_FREQUENCY:
        return SETTINGS.getRefreshFrequency();
      case XP_UI_READER_RENDER_FREE_HEAP:
        return ESP.getFreeHeap();
      case XP_UI_READER_RENDER_MAX_BLOCK:
        return ESP.getMaxAllocHeap();
      default:
        assert(frame);  // Remaining queries belong to a bound Page render.
        return frame->read(query);
    }
  }
  uint32_t execute(const XpUiReaderRenderOp& op) {
    const auto refresh = op.mode == XP_UI_HALF ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
    switch (static_cast<XpUiReaderRenderOperation>(op.kind)) {
      case XP_UI_READER_RENDER_CLEAR:
        renderer.clearScreen(op.value);
        return 0;
      case XP_UI_READER_RENDER_DISPLAY:
        renderer.displayBuffer(refresh);
        return 0;
      case XP_UI_READER_RENDER_DISPLAY_ASYNC:
        renderer.displayBufferAsync(refresh);
        return 0;
      case XP_UI_READER_RENDER_CLEANUP:
        renderer.cleanupGrayscaleWithFrameBuffer();
        return 0;
      case XP_UI_READER_RENDER_STORE_BW:
        return renderer.storeBwBuffer();
      default:
        assert(frame);  // Remaining operations borrow the bound Page and scratch.
        return frame->execute(op);
    }
  }
};
NativeResources& resources(void* handle) { return *static_cast<NativeResources*>(handle); }
BookHandle& book(void* handle) { return *static_cast<BookHandle*>(handle); }
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

MappedInputManager::Button inputButton(uint32_t index) {
  using Button = MappedInputManager::Button;
  constexpr Button buttons[] = {Button::Back, Button::Confirm, Button::Left, Button::Right,
                                Button::Up,   Button::Down,    Button::Power};
  assert(index < std::size(buttons));
  return buttons[index];
}

void readMargins(GfxRenderer& renderer, int32_t* out) {
  int margins[4]{};
  renderer.getOrientedViewableTRBL(&margins[0], &margins[1], &margins[2], &margins[3]);
  for (size_t i = 0; i < std::size(margins); ++i) out[i] = margins[i];
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
      .resources_open = []() -> void* { return makeUniqueNoThrow<NativeResources>(*target).release(); },
      .resources_close = [](void* handle) { delete static_cast<NativeResources*>(handle); },
      .render_services = [](void* handle) -> const XpUiReaderRenderServices* {
        return &resources(handle).renderServices;
      },
      .render_bind_page =
          [](void* handle, const XpEpubPageView* page, int32_t font, int32_t left, int32_t top, int32_t bottom,
             XpEpubHighlight highlight, void* context) {
            auto& root = resources(handle);
            assert(!root.frame && page && highlight);
            root.frame.emplace(*target, *page, font, left, top, bottom, highlight, context
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
                               ,
                               root.strips
#endif
            );
          },
      .render_finish_page =
          [](void* handle) {
            auto& root = resources(handle);
            assert(root.frame);
            root.frame.reset();
          },
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
            out->has_ttf = !target->getTtfFonts().empty();
            out->has_font_cache = target->getFontCacheManager() != nullptr;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
            out->strip_enabled = true;
#endif
          },
      .render_restore_bw = [](uint8_t resync) { target->restoreBwBuffer(resync != 0); },
      .render_discard_bw = [] { target->discardStoredBwBuffer(); },
      .render_orientation = [](uint8_t orientation) { ReaderUtils::applyOrientation(*target, orientation); },
      .loan_count = [] { return target->frameBufferLoanCount(); },
      .strip_release_idle = [](void* handle) -> uint8_t {
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        return resources(handle).strips.releaseIdle();
#else
        (void)handle;
        return false;
#endif
      },
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
      .protection_open = [](XpAppText path, XpAppTextSink error, void* context) -> void* {
        std::string message;
        auto protection = freeink::content::openProtectedBook(owned(path), message);
        if (!message.empty()) error(context, text(message));
        return protection.release();
      },
      .protection_close = [](void* protection) { delete static_cast<freeink::content::ContentDecryptor*>(protection); },
      .protection_encrypted = [](void* protection, XpAppText path) -> uint8_t {
        return static_cast<freeink::content::ContentDecryptor*>(protection)->isEncrypted(owned(path));
      },
      .protection_size = [](void* protection, XpAppText path) -> uint32_t {
        return static_cast<freeink::content::ContentDecryptor*>(protection)->decryptedSize(owned(path));
      },
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
      .file_seek = [](void* file, uint32_t offset) -> uint8_t { return static_cast<HalFile*>(file)->seek(offset); },
      .file_size = [](void* file) -> uint32_t { return static_cast<HalFile*>(file)->size(); },
      .path_remove_tree = [](XpAppText path) -> uint8_t { return Storage.removeDir(pathText(path)); },
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
      .paint_dither = [](int32_t x, int32_t y, int32_t width,
                         int32_t height) { target->fillRectDither(x, y, width, height, Color::LightGray); },
      .paint_pixel = [](int32_t x, int32_t y) { target->drawPixel(x, y, false); },
      .paint_status =
          [](const XpEpubStatus* value) {
            GUI.drawStatusBar(*target, value->book_progress, value->page, value->pages, owned(value->title), 0,
                              value->y_offset, true, value->bookmarked != 0, value->building != 0);
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
      .font_sizes = [](XpAppText family, XpAppTextSink sink, void* context) -> uint8_t {
        // Families borrow NUL-terminated owner or settings storage.
        const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), pathText(family));
        return sink(context, {sizes.data(), sizes.size()});
      },
      .settings_snapshot =
          [](uint8_t resolveFont, XpEpubSettings* out) {
            const auto& settings = SETTINGS;
            *out = {.font = resolveFont ? settings.getReaderFontId() : 0,
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
                    .font_family = text(settings.sdFontFamilyName)};
          },
      .settings_render_spec =
          [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
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
          },
      .settings_orientation = [](uint8_t value) { SETTINGS.orientation = value; },
      .settings_font_size = [](uint8_t value) { SETTINGS.fontPointSize = value; },
      .settings_line_spacing = [](uint8_t value) { SETTINGS.lineSpacing = value; },
      .settings_alignment = [](uint8_t value) { SETTINGS.paragraphAlignment = value; },
      .settings_focus_reading = [](uint8_t value) { SETTINGS.focusReadingEnabled = value; },
      .settings_inversion = [](uint8_t value) { SETTINGS.screenInverted = value; },
      .settings_ble_enabled = [](uint8_t value) { SETTINGS.ble.enabled = value; },
      .settings_save = [] { SETTINGS.saveToFile(); },
      .input_services = []() -> const XpUiReaderInputServices* {
        static const auto services = ReaderUtils::inputServices(*mapped);
        return &services;
      },
      .input_facts =
          [](XpEpubInputFacts* out) {
            *out = {};
            out->held_ms = mapped->getHeldTime();
            for (uint8_t button = HalGPIO::BTN_BACK; button <= HalGPIO::BTN_POWER; ++button) {
              if (gpio.isPressed(button)) out->raw_held |= 1u << button;
            }
            out->raw_edges = gpio.wasAnyPressed() || gpio.wasAnyReleased();
            out->remote_click = mapped->hasRemoteClick();
            out->touch = mapped->hasTouch();
            out->board_touch = BoardConfig::hasTouch();
            out->home_action = static_cast<uint8_t>(mapped->homeButtonAction());
            out->slide_owns_light = slideSwitchOwnsLight();
          },
      .input_pressed = [](uint32_t button) -> uint8_t { return mapped->isPressed(inputButton(button)); },
      .input_screen_long = [](int32_t* x, int32_t* y) -> uint8_t {
        int hitX = 0, hitY = 0;
        if (!mapped->wasScreenLongPress(hitX, hitY)) return 0;
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
      .trusted_now = [] { return trustedtime::trustedNow(); },
      .trusted_sync = [](uint32_t timeout) -> uint8_t { return trustedtime::syncNow(timeout); },
      .wifi_connected = []() -> uint8_t { return WiFi.status() == WL_CONNECTED; },
      .wifi_disconnect = [](uint8_t off) { WiFi.disconnect(off != 0); },
      .restart_reader = [] { silentRestartToReader(); },
      .child_menu = [](const XpEpubMenuArgs* args) -> void* {
        return makeLegacyActivity<EpubReaderMenuActivity>(
                   *target, *mapped, owned(args->title), args->display_page, args->pages, args->percent,
                   args->orientation, args->footnotes != 0, args->bookmarks != 0, args->clippings != 0)
            .release();
      },
      .child_chapter = [](void* handle, int32_t toc) -> void* {
        return makeLegacyActivity<EpubReaderChapterSelectionActivity>(*target, *mapped, book(handle).book, toc)
            .release();
      },
      .child_percent = [](int32_t percent) -> void* {
        return makeLegacyActivity<EpubReaderPercentSelectionActivity>(*target, *mapped, percent).release();
      },
      .child_text_settings = []() -> void* {
        return makeLegacyActivity<TextSettingsActivity>(*target, *mapped, &sdFontSystem.registry(),
                                                        TextSettingsActivity::Tab::Family)
            .release();
      },
      .child_footnote = [](XpEpubOwnedPage* page, int32_t left, int32_t top) -> void* {
        return epubPage::footnote(*target, *mapped, *page, left, top);
      },
      .child_dictionary = [](XpEpubOwnedPage* page, int32_t left, int32_t top, XpAppText initial) -> void* {
        return epubPage::dictionary(*target, *mapped, *page, left, top, initial);
      },
      .child_selection = [](XpEpubOwnedPage* pages, uint8_t count, const XpEpubSelectionArgs* args) -> void* {
        return epubPage::selection(*target, *mapped, pages, count, *args);
      },
      .child_bookmarks = [](void* handle, XpAppText path) -> void* {
        return makeLegacyActivity<EpubReaderBookmarksActivity>(*target, *mapped, book(handle).book, owned(path))
            .release();
      },
      .child_clippings = []() -> void* {
        return makeLegacyActivity<EpubReaderClippingListActivity>(*target, *mapped).release();
      },
      .child_qr = [](XpAppText payload) -> void* {
        return makeLegacyActivity<QrDisplayActivity>(*target, *mapped, owned(payload)).release();
      },
      .child_wifi = []() -> void* { return makeLegacyActivity<WifiSelectionActivity>(*target, *mapped).release(); },
      .child_sync = [](XpAppText path, const XpEpubPosition* current, const XpEpubSavedProgress* saved,
                       XpAppText chapter, uint8_t eventsOnly) -> void* {
        return makeLegacyActivity<KOReaderSyncActivity>(*target, *mapped, owned(path), position(*current),
                                                        SavedProgressPosition{owned(saved->xpath), saved->percentage},
                                                        owned(chapter), eventsOnly != 0)
            .release();
      },
      .toolbar_open = []() -> void* { return makeUniqueNoThrow<ReaderToolbarUi>(*target).release(); },
      .toolbar_close = [](void* handle) { delete static_cast<ReaderToolbarUi*>(handle); },
      .toolbar_begin = [](void* handle) { static_cast<ReaderToolbarUi*>(handle)->begin(); },
      .toolbar_paint = [](void* handle, const XpEpubToolbarModel* source, XpEpubRowSource rows,
                          void* context) -> uint8_t {
        auto& toolbar = *static_cast<ReaderToolbarUi*>(handle);
        struct RowSource {
          XpEpubRowSource rows;
          void* context;
        } rowSource{rows, context};
        ReaderToolbarUi::Model model;
        model.chapterTitle = source->chapter.data ? pathText(source->chapter) : nullptr;
        model.pageInfo = source->page_info.data ? pathText(source->page_info) : nullptr;
        model.panelTitle = source->panel_title.data ? pathText(source->panel_title) : nullptr;
        model.progressPermille = source->progress_permille;
        model.itemCount = source->item_count;
        model.selectedIndex = source->selected;
        model.activeTool = source->active_tool;
        model.panel = source->panel != 0;
        model.denseRows = source->dense != 0;
        model.rowFillContext = &rowSource;
        model.rowFill = [](void* raw, int index, std::string& label, std::string& value,
                           ReaderToolbarUi::RowFlags& flags) -> bool {
          const auto& source = *static_cast<const RowSource*>(raw);
          struct Output {
            std::string& label;
            std::string& value;
            ReaderToolbarUi::RowFlags& flags;
            bool copied = false;
          } output{label, value, flags};
          const auto copy = [](void* raw, const XpEpubToolbarRow* row) -> uint8_t {
            auto& out = *static_cast<Output*>(raw);
            assert(!out.copied);
            out.label.assign(row->label_spaces, ' ');
            out.label.append(view(row->label));
            out.value.assign(view(row->value));
            out.flags = {row->toggle != 0, row->checked != 0};
            out.copied = true;
            return true;
          };
          return source.rows && source.rows(source.context, index, copy, &output) && output.copied;
        };
        toolbar.setModel(model);
        ScopedCleanup clearModel{[&toolbar] { toolbar.setModel({}); }};
        return toolbar.render();
      },
      .toolbar_route =
          [](void* handle, XpEpubRouted* out) {
            const auto routed = static_cast<ReaderToolbarUi*>(handle)->route(*mapped);
            *out = {routed.value, routed.permille, routed.x, routed.y, static_cast<uint8_t>(routed.event),
                    routed.routed};
          },
      .toolbar_scroll = [](void* handle, int32_t delta, int32_t count) -> uint8_t {
        return static_cast<ReaderToolbarUi*>(handle)->scrollNavigation(delta, count);
      },
      .toolbar_visible_rows = [](void* handle) -> int32_t {
        return static_cast<ReaderToolbarUi*>(handle)->visibleRows();
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
      .bookmarks_open = []() -> void* { return makeUniqueNoThrow<Bookmarks>().release(); },
      .bookmarks_close = [](void* handle) { delete static_cast<Bookmarks*>(handle); },
      .bookmarks_clear = [](void* handle) { bookmarkRows(handle).clear(); },
      .bookmarks_load = [](void* handle, XpAppText path) -> uint8_t {
        return BookmarkFile::load(owned(path), bookmarkRows(handle));
      },
      .bookmarks_save = [](void* handle, XpAppText path) -> uint8_t {
        return BookmarkFile::save(owned(path), bookmarkRows(handle));
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
      .clippings_load = [](const XpAppBook* value) -> uint8_t {
        return CLIPPINGS.loadForBook(view(value->path), view(value->title), view(value->author), "epub");
      },
      .clippings_unload = [] { CLIPPINGS.unload(); },
      .clippings_count = []() -> uint32_t { return CLIPPINGS.clippingCount(); },
      .clippings_row =
          [](uint32_t index, XpEpubClip* out) {
            const auto* row = CLIPPINGS.clippingAt(index);
            assert(row);
            *out = {row->spineIndex,   row->startPage,       row->endPage,     row->pageCount, row->startWordIndex,
                    row->endWordIndex, row->layoutSignature, row->startOffset, row->endOffset};
          },
      .clippings_remove = [](uint32_t index) -> uint8_t { return CLIPPINGS.removeClippingAt(index); },
      .clippings_insert = [](const XpEpubClipInsert* row) -> uint8_t {
        const auto& range = row->range;
        return static_cast<uint8_t>(
            CLIPPINGS.addClipping(range.spine, range.start_page, range.end_page, range.page_count, range.start_word,
                                  range.end_word, row->word_count, pathText(row->chapter), row->paragraph,
                                  borrowed(row->text), range.signature, range.start_offset, range.end_offset));
      },
      .clippings_export = [](XpAppText title, XpAppText author, XpAppText chapter, int32_t percent,
                             XpAppNativeText value) -> uint8_t {
        return ClippingsManager::saveClipping(owned(title), owned(author), owned(chapter), percent, borrowed(value));
      },
      .clippings_move_book = [](XpAppText from, XpAppText to) -> uint8_t {
        return ClippingStore::moveBook(owned(from), owned(to));
      },
      .recents_remove = [](XpAppText path) -> uint8_t { return RECENT_BOOKS.removeByPath(owned(path)); },
      .recents_add =
          [](const XpAppBook* value) {
            RECENT_BOOKS.addBook(view(value->path), view(value->title), view(value->author), view(value->cover));
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
      .state_reset_reader_count = [] { APP_STATE.readerActivityLoadCount = 0; },
      .state_save = [] { APP_STATE.saveToFile(); },
      .has_sync_credentials = []() -> uint8_t { return KOREADER_STORE.hasCredentials(); },
      .has_exit_subscriber = []() -> uint8_t { return pluginevents::anySubscriber(pluginevents::Event::ReaderExit); },
      .has_session_subscriber = []() -> uint8_t {
        return pluginevents::anySubscriber(pluginevents::Event::ReaderSession);
      },
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
      .font_text_advance = [](XpAppText value, int32_t font, uint8_t style, int8_t spacing) -> int32_t {
        return target->getTextAdvanceX(font, pathText(value), static_cast<EpdFontFamily::Style>(style), spacing);
      },
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
      .toolbar_reset_navigation =
          [](void* handle, int32_t selected, int32_t top) {
            static_cast<ReaderToolbarUi*>(handle)->resetNavigation(selected, top);
          },
      .input_snapshot = [](XpUiInput* out) { *out = rustInputSnapshot(*mapped); },
      .cache_lock = [] { BookMetadataCache::lockReadCursor(); },
      .cache_unlock = [] { BookMetadataCache::unlockReadCursor(); },
      .scratch_claim = [](uint32_t min_len) { return buildscratch::claim(min_len); },
      .scratch_release = [](const uint8_t* bytes) { buildscratch::release(bytes); },
      .inflate_oom_log = [](XpAppText allocation,
                            uint32_t bytes) { InflateStream::logAllocationFailure(pathText(allocation), bytes); },
      .build_metrics = []() -> const XpParaMetrics* { return &sectionMetrics; },
      .build_allocator = paragraphAllocator,
      .font_variant = [](int32_t font, float scale) -> int32_t { return target->resolveFontVariant(font, scale); },
      .font_line_height = [](int32_t font, float compression) -> int32_t {
        return target->getLineHeight(font, compression);
      },
      .font_ready = [](int32_t font, int32_t fallback) -> int32_t {
        return font == fallback || target->ensureFontLoaded(font) ? font : fallback;
      },
      .font_codepoint = [](int32_t font, uint32_t codepoint, uint8_t style, int32_t* advance, int32_t* top) -> uint8_t {
        int32_t measuredAdvance = 0;
        int measuredTop = 0;
        const bool found = target->getCodepointMetrics(font, codepoint, static_cast<EpdFontFamily::Style>(style),
                                                       measuredAdvance, measuredTop);
        *advance = measuredAdvance;
        *top = measuredTop;
        return found;
      },
      .hyphen_language = [](XpAppText language) -> int32_t {
        Hyphenator::setPreferredLanguage(owned(language));
        return Hyphenator::preferredLanguageId();
      },
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
      .paint_arena = [](const XpEpubPageView* page, int32_t font, int32_t left, int32_t top,
                        uint8_t mode) { epubPage::paint(*target, *page, font, left, top, mode); },
      .image_needs_decode = epubPage::imageNeedsDecode,
      .font_ascent = [](int32_t font) -> int32_t { return epubPage::fontAscent(*target, font); },
  };
  if (!sectionMetrics.context) sectionMetrics.context = target;
  return leaves;
}
