#include "ProgressMapper.h"

#include <Logging.h>

#include <cstring>

#include "ChapterXPathResolver.h"
#include "Epub/Section.h"
#include "Epub/htmlEntities.h"
#include "SyncBridge.h"
#include "Utf8.h"
namespace {
struct Context {
  const std::shared_ptr<Epub>& epub;
  GfxRenderer* renderer;
  SavedProgressPosition* saved = nullptr;
  std::optional<Section> section{};
  int sectionSpine = -1;
};
class ReverseStream final : public Print {
  XpSyncReverse* state;
  static int32_t entity(const uint8_t* data, size_t len) {
    const char* value = lookupHtmlEntity(reinterpret_cast<const char*>(data), len);
    if (!value) return -1;
    int32_t n = 0;
    auto ptr = reinterpret_cast<const unsigned char*>(value);
    while (*ptr) {
      utf8NextCodepoint(&ptr);
      ++n;
    }
    return n;
  }

 public:
  explicit ReverseStream(XpSyncReverse* state) : state(state) {}
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    xp_sync_reverse_feed(state, {data, len}, entity);
    return len;
  }
};
uint8_t stream(void* raw, int32_t spine, XpSyncReverse* state) {
  auto& c = *static_cast<Context*>(raw);
  const auto href = c.epub->getSpineItem(spine).href;
  c.section.reset();
  ReverseStream sink(state);
  return !href.empty() && c.epub->readItemContentsToStream(href, sink, 1024);
}
int64_t query(void* raw, uint32_t kind, int32_t spine, size_t arg, XpSyncSpan text) {
  auto& c = *static_cast<Context*>(raw);
  switch (static_cast<XpSyncQuery>(kind)) {
    case XP_SYNC_QUERY_BOOK_SIZE:
      return c.epub->getBookSize();
    case XP_SYNC_QUERY_SPINE_COUNT:
      return c.epub->getSpineItemsCount();
    case XP_SYNC_QUERY_SPINE_FOR_SIZE:
      return c.epub->getSpineIndexForSize(arg);
    case XP_SYNC_QUERY_CUMULATIVE_SIZE:
      return c.epub->getCumulativeSpineItemSize(spine);
    case XP_SYNC_QUERY_ITEM_SIZE: {
      size_t size = 0;
      const auto href = c.epub->getSpineItem(spine).href;
      return !href.empty() && c.epub->getItemSize(href, &size) ? static_cast<int64_t>(size) : -1;
    }
    default:
      break;
  }
  if (!c.section || c.sectionSpine != spine) {
    c.section.emplace(c.epub, spine, *c.renderer);
    c.sectionSpine = spine;
  }
  const auto& section = *c.section;
  switch (static_cast<XpSyncQuery>(kind)) {
    case XP_SYNC_QUERY_CACHED_PAGES: {
      auto n = section.getCachedPageCount();
      return n ? *n : -1;
    }
    case XP_SYNC_QUERY_PAGE_FOR_OFFSET: {
      uint32_t offset;
      memcpy(&offset, text.data, sizeof(offset));
      auto n = section.getPageForVisibleTextOffset(offset, arg != 0);
      return n ? *n : -1;
    }
    case XP_SYNC_QUERY_PAGE_FOR_PARAGRAPH: {
      auto n = section.getPageForParagraphIndex(arg);
      return n ? *n : -1;
    }
    case XP_SYNC_QUERY_PAGE_FOR_LIST: {
      auto n = section.getPageForListItemIndex(arg);
      return n ? *n : -1;
    }
    case XP_SYNC_QUERY_PAGE_FOR_ANCHOR: {
      auto n = section.getPageForAnchor(std::string(reinterpret_cast<const char*>(text.data), text.len));
      return n ? *n : -1;
    }
    default:
      return -1;
  }
}
// Mapping events preserve PM diagnostics; a/b/c/d are the branch's integer details.
void diagnostic(void*, const XpSyncPosition* p, const XpSyncMapEvent* e) {
  const char* text = reinterpret_cast<const char*>(e->text.data);
  const char* anchor = reinterpret_cast<const char*>(p->anchor);
  switch (static_cast<XpSyncMapDiagnostic>(e->id)) {
    case XP_SYNC_MAP_DIAG_UNUSED:
      break;
    case XP_SYNC_MAP_DIAG_ANCESTRY:
      LOG_DBG("PM", "XPath ancestry(%.*s[%d])/text()[%d]+%d -> %.1f%% (target=%zu total=%zu p~%d li~%d anchor=%s)",
              static_cast<int>(e->text.len), text, e->a, e->b, e->c, e->ratio * 100, e->target, e->total, e->d,
              p->has_li ? static_cast<int>(p->li) : 0, anchor[0] ? anchor : "none");
      break;
    case XP_SYNC_MAP_DIAG_BODY_TEXT:
      LOG_DBG("PM", "XPath body/text()[%d]+%d -> offset=%u", e->b, e->c, p->offset);
      break;
    case XP_SYNC_MAP_DIAG_PARAGRAPH:
      LOG_DBG("PM", "XPath p[%d]/text()[%d]+%d -> %.1f%% (target=%zu total=%zu)", e->a, e->b, e->c, e->ratio * 100,
              e->target, e->total);
      break;
    case XP_SYNC_MAP_DIAG_CHAPTER_START:
      LOG_DBG("PM", "Chapter-start XPath %.*s -> spine=%d page start", static_cast<int>(e->text.len), text, p->spine);
      break;
    case XP_SYNC_MAP_DIAG_CONTENT_OFFSET:
      LOG_DBG("PM", "XPath content offset %u -> spine=%d page=%d/%d", p->offset, p->spine, p->page, p->pages);
      break;
    case XP_SYNC_MAP_DIAG_NO_OFFSET_LUT:
      LOG_DBG("PM", "No page-offset LUT for spine=%d offset=%u; using percentage fallback", p->spine, p->offset);
      break;
    case XP_SYNC_MAP_DIAG_PERCENTAGE:
      LOG_DBG("PM", "<- Progress: %.2f%% %.*s -> spine=%d page=%d/%d", e->ratio * 100, static_cast<int>(e->text.len),
              text, p->spine, p->page, p->pages);
      break;
    case XP_SYNC_MAP_DIAG_LIST_PAGE:
      LOG_DBG("PM", "Li index %u -> page %d (was %d)", p->li, e->a, p->page);
      break;
    case XP_SYNC_MAP_DIAG_NO_LIST_PAGE:
      LOG_DBG("PM", "Li index %u not found in section LUT", p->li);
      break;
    case XP_SYNC_MAP_DIAG_ANCHOR_PAGE:
      LOG_DBG("PM", "Anchor '%s' -> page %d (was %d)", anchor, e->a, p->page);
      break;
    case XP_SYNC_MAP_DIAG_NO_ANCHOR_PAGE:
      LOG_DBG("PM", "Anchor '%s' not found in section cache", anchor);
      break;
    case XP_SYNC_MAP_DIAG_PARAGRAPH_PAGE: {
      char next[8];
      if (e->b >= 0)
        snprintf(next, sizeof(next), "%d", e->b);
      else
        snprintf(next, sizeof(next), "none");
      LOG_DBG("PM", "Paragraph %u -> LUT page %d, nextPara page %s, intra page %d, using %d", p->paragraph, e->a, next,
              p->page, e->c);
      break;
    }
    case XP_SYNC_MAP_DIAG_NO_PARAGRAPH_PAGE:
      LOG_DBG("PM", "Paragraph %u not found in section LUT", p->paragraph);
      break;
    case XP_SYNC_MAP_DIAG_RICH_OUT_OF_RANGE:
      LOG_DBG("PM", "Rich position spine %u out of range (%d spine items)", static_cast<unsigned>(p->spine), e->a);
      break;
    case XP_SYNC_MAP_DIAG_RICH_NO_CACHE:
      LOG_DBG("PM", "Rich position spine %u has no cached page count", static_cast<unsigned>(p->spine));
      break;
    case XP_SYNC_MAP_DIAG_RICH_EXACT:
      LOG_DBG("PM", "Rich position exact: spine=%d page=%d/%d", p->spine, p->page, p->pages);
      break;
    case XP_SYNC_MAP_DIAG_RICH_PARAGRAPH:
      LOG_DBG("PM", "Rich position para %u -> spine=%d page=%d/%d", p->paragraph, p->spine, p->page, p->pages);
      break;
    case XP_SYNC_MAP_DIAG_RICH_SCALED:
      LOG_DBG("PM", "Rich position scaled: spine=%d remote %u/%d -> page=%d/%d", p->spine, static_cast<unsigned>(e->a),
              e->b, p->page, p->pages);
      break;
  }
}
float progress(void* raw, int32_t spine, float intra) {
  return static_cast<Context*>(raw)->epub->calculateProgress(spine, intra);
}
uint8_t xpath(void* raw, int32_t spine, uint32_t mode, uint32_t value, float ratio) {
  auto& c = *static_cast<Context*>(raw);
  switch (mode) {
    case 0:
      c.saved->xpath = ChapterXPathResolver::findXPathForParagraph(c.epub, spine, value);
      break;
    case 1:
      c.saved->xpath = ChapterXPathResolver::findXPathForVisibleTextOffset(c.epub, spine, value);
      break;
    case 2:
      c.saved->xpath = ChapterXPathResolver::findXPathForProgress(c.epub, spine, ratio);
      break;
  }
  return !c.saved->xpath.empty();
}
void append(void* raw, XpSyncSpan text) {
  static_cast<Context*>(raw)->saved->xpath.append(reinterpret_cast<const char*>(text.data), text.len);
}
}  // namespace
SavedProgressPosition ProgressMapper::toSavedProgress(const std::shared_ptr<Epub>& epub,
                                                      const CrossPointPosition& pos) {
  SavedProgressPosition result;
  Context context{epub, nullptr, &result};
  const XpSyncSaveServices services{{&context, query, stream, diagnostic}, progress, xpath, append};
  const auto input = syncPosition(pos);
  result.percentage = xp_sync_save(&services, &input);
  LOG_DBG("PM", "-> Progress: spine=%d page=%d/%d %.2f%% %s", pos.spineIndex, pos.pageNumber, pos.totalPages,
          static_cast<double>(result.percentage * 100), result.xpath.c_str());
  return result;
}
CrossPointPosition ProgressMapper::toCrossPoint(const std::shared_ptr<Epub>& epub, const SavedProgressPosition& saved,
                                                GfxRenderer& renderer, int current, int pages, int fallback) {
  Context context{epub, &renderer};
  const XpSyncMapServices services{&context, query, stream, diagnostic};
  XpSyncPosition out{};
  xp_sync_map(&services, syncSpan(saved.xpath), saved.percentage, current, pages, fallback, &out);
  return syncPosition(out);
}
std::optional<CrossPointPosition> ProgressMapper::fromRichPosition(const std::shared_ptr<Epub>& epub,
                                                                   const KOReaderRichPosition& rich,
                                                                   GfxRenderer& renderer, bool tried) {
  Context context{epub, &renderer};
  const XpSyncMapServices services{&context, query, stream, diagnostic};
  XpSyncPosition out{};
  const XpSyncRich input{rich.pctQ,
                         rich.spineIndex,
                         rich.pageNumber,
                         rich.totalPages,
                         rich.paragraphIndex.value_or(0),
                         rich.paragraphIndex.has_value()};
  if (!xp_sync_rich(&services, &input, syncSpan(rich.xpath), tried, &out)) return std::nullopt;
  return syncPosition(out);
}
