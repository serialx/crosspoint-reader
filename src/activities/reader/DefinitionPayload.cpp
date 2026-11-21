#include "DefinitionPayload.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <new>

#include "ClipSelectionActivity.h"
#include "CrossPointSettings.h"
#include "activities/OwnedLegacyActivity.h"
#include "components/UITheme.h"
#include "util/DictHtmlPages.h"
#include "util/Dictionary.h"
#include "util/HtmlToPlainText.h"
static_assert(sizeof(DefinitionPayload) <= sizeof(XpDefinitionPayloadStorage));
static_assert(alignof(DefinitionPayload) <= alignof(XpDefinitionPayloadStorage));
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(DefinitionPayload) == 172);
static_assert(sizeof(DefinitionPayload::Slot) == 80);
static_assert(sizeof(XpDefinitionNativeTransfer) == 12);
#endif
DefinitionPayload& DefinitionPayload::get(XpDefinitionPayloadStorage* storage) {
  return *std::launder(reinterpret_cast<DefinitionPayload*>(storage));
}
const DefinitionPayload& DefinitionPayload::get(const XpDefinitionPayloadStorage* storage) {
  return *std::launder(reinterpret_cast<const DefinitionPayload*>(storage));
}
DefinitionPayload::Slot& DefinitionPayload::slot(uint8_t index) {
  assert(index < 2);
  return slots[index];
}
const DefinitionPayload::Slot& DefinitionPayload::slot(uint8_t index) const {
  assert(index < 2);
  return slots[index];
}
void DefinitionPayload::Slot::releasePayload() {
  text.emplace<std::string>();
  std::vector<std::unique_ptr<Page>>{}.swap(pages);
  std::vector<Line>{}.swap(lines);
}
namespace {

// Longest measurable/drawable span. Wrapped lines stay under the screen width
// (far below this); only pathological unbreakable tokens are split at this cap.
constexpr size_t MAX_LINE_BYTES = 191;
constexpr float PLAIN_TEXT_LINE_SPACING = 1.5f;
constexpr size_t HEAP_HEADROOM = 8 * 1024;
constexpr size_t SELECTION_PAGE_WINDOW = 3;
// ASCII tokens need separators; Han/Kana tokens take at least three bytes.
constexpr size_t MAX_PLAIN_WORDS = (MAX_LINE_BYTES + 1) / 2;

// Styled-path ceiling: the laid-out Pages keep the whole definition resident
// (TextBlock arenas ≈ text + ~7 bytes/word plus per-line objects), roughly
// doubling the string's footprint while this activity is stacked over the
// reader and word-select. Bigger definitions take the span-based plain-text
// path, which holds no per-page copies.
constexpr size_t MAX_STYLED_HTML_BYTES = 16 * 1024;

}  // namespace

// Heap-owned only while preparing plain selection pages; keep the loop task's stack small.
struct DefinitionPayload::PlainSelectionLine {
  std::string text;
  std::vector<std::string> words;
  std::vector<int16_t> positions;
  std::vector<TextBlock::SourceRange> ranges;
  std::vector<uint16_t> visualOrder;
  std::vector<std::string> visualWords;
  std::vector<TextBlock::SourceRange> visualRanges;
  BlockStyle style;

  static bool separator(const char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == DICTIONARY_PLAIN_HEADWORD_MARKER;
  }

  static size_t wordEnd(const char* bytes, const size_t length, size_t i) {
    auto* ptr = reinterpret_cast<const unsigned char*>(bytes + i);
    uint32_t previous = utf8NextCodepoint(&ptr);
    i = ptr - reinterpret_cast<const unsigned char*>(bytes);
    while (i < length && !separator(bytes[i])) {
      ptr = reinterpret_cast<const unsigned char*>(bytes + i);
      const uint32_t cp = utf8NextCodepoint(&ptr);
      if (!utf8IsHangul(previous) && !utf8IsHangul(cp) && (utf8IsCjkBreakable(previous) || utf8IsCjkBreakable(cp)) &&
          !utf8IsCombiningMark(cp))
        break;
      previous = cp;
      i = ptr - reinterpret_cast<const unsigned char*>(bytes);
    }
    return i;
  }

  static void countWords(const char* bytes, const size_t length, size_t& words, size_t& textBytes) {
    for (size_t i = 0; i < length;) {
      if (separator(bytes[i])) {
        ++i;
        continue;
      }
      const size_t end = wordEnd(bytes, length, i);
      ++words;
      textBytes += end - i + 1;
      i = end;
    }
  }

  void layout(const GfxRenderer& renderer, const int fontId, const uint32_t offset) {
    const char* bytes = text.c_str();
    const size_t length = text.size();
    words.clear();
    positions.clear();
    ranges.clear();
    size_t i = 0;
    while (i < length) {
      // The headword label marker is a control byte, not text: it must not
      // end up inside a selected word.
      if (separator(bytes[i])) {
        ++i;
        continue;
      }
      const size_t start = i;
      i = wordEnd(bytes, length, i);
      words.emplace_back(bytes + start, i - start);
      ranges.push_back({static_cast<uint32_t>(offset + start), static_cast<uint32_t>(offset + i)});
    }
    style.isRtl = BidiUtils::detectParagraphLevel(bytes) == 1;
    const bool reordered = BidiUtils::computeVisualWordOrder(words, style.isRtl, visualOrder);
    int x = 0;
    for (size_t n = 0; n < words.size(); ++n) {
      const size_t index = reordered ? visualOrder[n] : n;
      if (n > 0) {
        const size_t previous = reordered ? visualOrder[n - 1] : n - 1;
        const size_t left = std::min(previous, index);
        const size_t right = std::max(previous, index);
        x += right == left + 1 ? measureSpan(renderer, fontId, text.c_str() + ranges[left].end - offset,
                                             ranges[right].start - ranges[left].end)
                               : renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
      }
      positions.push_back(x);
      x += renderer.getTextAdvanceX(fontId, words[index].c_str(), EpdFontFamily::REGULAR);
    }
    if (reordered) {
      visualWords.clear();
      visualRanges.clear();
      for (const auto index : visualOrder) {
        visualWords.push_back(std::move(words[index]));
        visualRanges.push_back(ranges[index]);
      }
      words.swap(visualWords);
      ranges.swap(visualRanges);
    }
  }

  bool append(LinePageBuilder& builder, const int16_t y) {
    if (words.size() > MAX_PLAIN_WORDS) return false;
    text.clear();
    visualOrder.clear();
    for (const auto& word : words) {
      if (word.size() + 1 > MAX_LINE_BYTES + MAX_PLAIN_WORDS - text.size()) return false;
      visualOrder.push_back(text.size());
      text.append(word);
      text.push_back('\0');
    }
    static constexpr uint8_t STYLES[MAX_PLAIN_WORDS] = {};
    XpParaBlock block{};
    block.rtl = style.isRtl;
    XpParaLine line{};
    line.text = {reinterpret_cast<const uint8_t*>(text.data()), static_cast<uint32_t>(text.size())};
    line.offsets = visualOrder.data();
    line.count = words.size();
    line.positions = positions.data();
    line.styles = STYLES;
    line.sources = reinterpret_cast<const uint32_t*>(ranges.data());
    line.block = &block;
    line.paragraph_start = UINT16_MAX;
    return builder.addLine(line, 0, y);
  }
};

bool DefinitionPayload::prepareDefinition(GfxRenderer& renderer, Slot& next, const bool htmlDefinition,
                                          const bool forcePlain, const XpUiRect body) {
  const auto view = next.textView();
  // Both owners provide mutable, NUL-terminated storage until layout completes.
  if (!view.empty()) {
    auto* begin = const_cast<char*>(view.data());
    std::replace(begin, begin + view.size(), '\0', '\n');
  }
  if (htmlDefinition && !forcePlain && view.size() <= MAX_STYLED_HTML_BYTES && layoutHtmlPages(renderer, next, body))
    return true;
  if (htmlDefinition) {
    if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
    const auto heap = HalMemory::getDefaultHeap();
    if (heap.largestBlockBytes < view.size() + 1 || heap.freeBytes < view.size() + HEAP_HEADROOM) {
      LOG_ERR("DICT", "Low heap for plain definition (%u bytes)", static_cast<unsigned>(view.size() + 1));
      return false;
    }
    next.text = htmlToPlainText(view, DICTIONARY_PLAIN_HEADWORD_MARKER);
  }
  return wrapText(renderer, next, body);
}

bool DefinitionPayload::layoutHtmlPages(GfxRenderer& renderer, Slot& next, const XpUiRect body) {
  const auto definition = next.textView();
  auto& pages = next.pages;
  if (body.w <= 0 || body.h <= 0) {
    LOG_ERR("DHTML", "Invalid definition viewport: %dx%d", body.w, body.h);
    return false;
  }
  if (!buildDictionaryHtmlPages(renderer, definition, static_cast<uint16_t>(body.w), static_cast<uint16_t>(body.h),
                                pages)) {
    return false;
  }
  next.text.emplace<std::string>();
  return true;
}

int DefinitionPayload::measureSpan(const GfxRenderer& renderer, const int fontId, const char* text, size_t len,
                                   const EpdFontFamily::Style family) {
  char buf[MAX_LINE_BYTES + 1];
  len = std::min(len, MAX_LINE_BYTES);
  memcpy(buf, text, len);
  buf[len] = '\0';
  return renderer.getTextAdvanceX(fontId, buf, family);
}

bool DefinitionPayload::reserveLines(Slot& next, const size_t count) {
  if (next.lines.capacity() >= count) return true;
  const size_t bytes = count * sizeof(Line);
  const auto heap = HalMemory::getDefaultHeap();
  if (heap.largestBlockBytes < bytes || heap.freeBytes < bytes + HEAP_HEADROOM) {
    LOG_ERR("DICT", "Low heap for wrapped lines (%u bytes)", static_cast<unsigned>(bytes));
    return false;
  }
  next.lines.reserve(count);
  return true;
}

bool DefinitionPayload::wrapText(GfxRenderer& renderer, Slot& next, const XpUiRect body) {
  const auto definition = next.textView();
  auto& lines = next.lines;
  lines.clear();
  if (!reserveLines(next, definition.size() / 32 + 8)) return false;

  const int fontId = SETTINGS.getReaderFontId();
  // SD-card fonts: merge every definition codepoint into the persistent
  // advance table up front. Otherwise each unseen codepoint measured below
  // falls back to an on-demand glyph load from SD (8-slot overflow ring).
  renderer.ensureSdCardFontReady(fontId, definition.data(), 0x03 /* REGULAR + BOLD */);

  const int maxWidth = body.w;
  const int spaceWidth = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
  const int lineHeight = renderer.getLineHeight(fontId, PLAIN_TEXT_LINE_SPACING);
  next.linesPerPage = std::max(1, body.h / lineHeight);

  const char* text = definition.data();
  const uint32_t n = static_cast<uint32_t>(definition.size());
  uint32_t lineStart = 0;
  uint32_t lineEnd = 0;  // one past the last token byte on the current line
  int lineWidth = 0;
  bool lineBold = false;

  const auto flushLine = [&](uint32_t nextStart) {
    if (lines.size() == lines.capacity() && !reserveLines(next, lines.capacity() * 2)) return false;
    lines.push_back({lineStart, static_cast<uint16_t>(lineEnd - lineStart), lineBold});
    lineStart = nextStart;
    lineEnd = nextStart;
    lineWidth = 0;
    lineBold = false;
    return true;
  };

  uint32_t i = 0;
  while (i < n) {
    // A marker at the start of a line makes that whole line bold. It stands
    // for a <h2> in the HTML path, and for a synonym's headword label in the
    // plain-text path.
    if (lineEnd == lineStart && i == lineStart && text[i] == DICTIONARY_PLAIN_HEADWORD_MARKER) {
      lineStart = ++i;
      lineEnd = lineStart;
      lineBold = true;
      continue;
    }
    const char c = text[i];
    if (c == '\n' || c == '\0') {
      if (!flushLine(i + 1)) return false;
      i++;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      i++;
      continue;
    }

    // Token: run of non-whitespace bytes, capped at the measure buffer.
    const uint32_t tokenStart = i;
    while (i < n && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' && text[i] != '\0' &&
           i - tokenStart < MAX_LINE_BYTES) {
      i++;
    }
    // If the byte cap cut the token mid-UTF-8-sequence, back off to the last
    // complete codepoint so measure/draw never see a partial sequence. A
    // natural stop lands on whitespace or the terminating NUL, never on a
    // continuation byte, so this is a no-op there.
    while (i - tokenStart > 1 && (text[i] & 0xC0) == 0x80) i--;
    const uint32_t tokenLen = i - tokenStart;
    const int tokenWidth = measureSpan(renderer, fontId, text + tokenStart, tokenLen,
                                       lineBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    if (lineEnd == lineStart) {
      lineStart = tokenStart;
      lineEnd = tokenStart + tokenLen;
      lineWidth = tokenWidth;
    } else if (lineWidth + spaceWidth + tokenWidth <= maxWidth &&
               tokenStart + tokenLen - lineStart <= UINT16_MAX) {  // span len must fit Line::len
      lineEnd = tokenStart + tokenLen;
      lineWidth += spaceWidth + tokenWidth;
    } else {
      if (!flushLine(tokenStart)) return false;
      lineEnd = tokenStart + tokenLen;
      lineWidth = tokenWidth;
    }

    // An unbreakable token wider than the screen is now alone on the line
    // (any previous content was flushed above): split it at the widest
    // fitting UTF-8 boundary and carry the remainder forward.
    while (lineWidth > maxWidth && lineEnd - lineStart > 1) {
      const uint32_t len = lineEnd - lineStart;
      uint32_t lastFit = 0;
      for (uint32_t f = 1; f <= len; f++) {
        if (f == len || (text[lineStart + f] & 0xC0) != 0x80) {  // codepoint boundary
          if (measureSpan(renderer, fontId, text + lineStart, f,
                          lineBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR) > maxWidth)
            break;
          lastFit = f;
        }
      }
      if (lastFit == 0) {
        // Even a single over-wide glyph must make progress; consume its whole
        // UTF-8 sequence rather than splitting it into invalid fragments.
        lastFit = 1;
        while (lastFit < len && (text[lineStart + lastFit] & 0xC0) == 0x80) lastFit++;
      }
      const uint32_t rest = lineStart + lastFit;
      lineEnd = rest;
      if (!flushLine(rest)) return false;
      lineEnd = rest + (len - lastFit);
      lineWidth = measureSpan(renderer, fontId, text + lineStart, lineEnd - lineStart,
                              lineBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
  }
  if (lineEnd > lineStart && !flushLine(n)) return false;

  // Trim trailing blank lines so the last page is not empty padding.
  while (!lines.empty() && lines.back().len == 0) lines.pop_back();

  return true;
}

bool DefinitionPayload::buildPlainSelectionPages(GfxRenderer& renderer, const int selectionPage) {
  const auto& content = slots[0];
  // Only the selection window gets packed word arenas; the definition keeps its cheap line spans.
  const int fontId = SETTINGS.getReaderFontId();
  const int lineHeight = renderer.getLineHeight(fontId, PLAIN_TEXT_LINE_SPACING);
  const int pageCount = std::min<int>(SELECTION_PAGE_WINDOW, content.totalPages() - selectionPage);
  const size_t scratchBytes = sizeof(PlainSelectionLine) + MAX_LINE_BYTES + MAX_PLAIN_WORDS + 1 +
                              MAX_PLAIN_WORDS * (2 * sizeof(std::string) + sizeof(int16_t) +
                                                 2 * sizeof(TextBlock::SourceRange) + sizeof(uint16_t) + 4);
  auto heap = HalMemory::getDefaultHeap();
  if (heap.freeBytes < scratchBytes + HEAP_HEADROOM || heap.largestBlockBytes < scratchBytes) return false;
  auto scratch = makeUniqueNoThrow<PlainSelectionLine>();
  if (!scratch) {
    LOG_ERR("DICT", "OOM: selection scratch (%u bytes)", static_cast<unsigned>(sizeof(PlainSelectionLine)));
    return false;
  }
  scratch->text.reserve(MAX_LINE_BYTES + MAX_PLAIN_WORDS);
  scratch->words.reserve(MAX_PLAIN_WORDS);
  scratch->positions.reserve(MAX_PLAIN_WORDS);
  scratch->ranges.reserve(MAX_PLAIN_WORDS);
  scratch->visualOrder.reserve(MAX_PLAIN_WORDS);
  scratch->visualWords.reserve(MAX_PLAIN_WORDS);
  scratch->visualRanges.reserve(MAX_PLAIN_WORDS);
  selectionPages.reserve(SELECTION_PAGE_WINDOW);
  size_t capacity = LinePageBuilder::capacityForLines(0, 0, 0);
  for (int p = 0; p < pageCount; ++p) {
    size_t lines = 0, words = 0, textBytes = 0;
    const int first = (selectionPage + p) * content.linesPerPage;
    const int last = std::min(first + content.linesPerPage, static_cast<int>(content.lines.size()));
    for (int row = first; row < last; ++row) {
      const auto& line = content.lines[row];
      const char* text = content.textView().data() + line.start;
      const size_t length = utf8SafeTruncateBuffer(text, std::min<size_t>(line.len, MAX_LINE_BYTES));
      scratch->text.assign(text, length);
      if (length) {
        ++lines;
        PlainSelectionLine::countWords(scratch->text.c_str(), length, words, textBytes);
      }
    }
    capacity = std::max(capacity, LinePageBuilder::capacityForLines(lines, words, textBytes));
  }
  if (capacity > SIZE_MAX - HEAP_HEADROOM) return false;
  heap = HalMemory::getDefaultHeap();
  if (heap.freeBytes < capacity + HEAP_HEADROOM || heap.largestBlockBytes < capacity) return false;
  LinePageBuilder builder(capacity);
  for (int p = 0; p < pageCount; ++p) {
    if (!builder.reset()) return false;
    const int firstLine = (selectionPage + p) * content.linesPerPage;
    const int lastLine = std::min(firstLine + content.linesPerPage, static_cast<int>(content.lines.size()));
    heap = HalMemory::getDefaultHeap();
    const size_t pageBytes = Page::plainLineAllocationSize(capacity, lastLine - firstLine);
    if (pageBytes > SIZE_MAX - HEAP_HEADROOM) return false;
    if (heap.freeBytes < pageBytes + HEAP_HEADROOM || heap.largestBlockBytes < pageBytes) return false;
    for (int row = firstLine; row < lastLine; ++row) {
      const auto& line = content.lines[row];
      if (!line.len) continue;
      const size_t length =
          utf8SafeTruncateBuffer(content.textView().data() + line.start, std::min<size_t>(line.len, MAX_LINE_BYTES));
      scratch->text.assign(content.textView().data() + line.start, length);
      heap = HalMemory::getDefaultHeap();
      const size_t lineBytes = length * (sizeof(TextBlock::SourceRange) + 12) + sizeof(TextBlock) + 1024;
      if (heap.freeBytes < lineBytes + HEAP_HEADROOM || heap.largestBlockBytes < lineBytes) return false;
      scratch->layout(renderer, fontId, line.start);
      if (!scratch->append(builder, (row - firstLine) * lineHeight)) {
        LOG_ERR("DICT", "OOM: selection line (%u byte budget)", static_cast<unsigned>(lineBytes));
        return false;
      }
    }
    auto page = builder.finish();
    if (!page) {
      LOG_ERR("DICT", "OOM: selection page (%u bytes)", static_cast<unsigned>(sizeof(Page)));
      return false;
    }
    selectionPages.push_back(std::move(page));
  }
  return true;
}

void DefinitionPayload::drawBody(GfxRenderer& renderer, const int fontId, const int x, const int startY,
                                 const int currentPage) const {
  const auto& content = slots[0];
  const auto& pages = content.pages;
  const auto& lines = content.lines;
  const auto definition = content.textView();
  const int linesPerPage = content.linesPerPage;
  if (!pages.empty()) {
    pages[currentPage]->render(renderer, fontId, x, startY);
    return;
  }
  const int lineHeight = renderer.getLineHeight(fontId, PLAIN_TEXT_LINE_SPACING);
  char buf[MAX_LINE_BYTES + 1];
  const int firstLine = currentPage * linesPerPage;
  const int lastLine = std::min(firstLine + linesPerPage, static_cast<int>(lines.size()));
  for (int i = firstLine; i < lastLine; i++) {
    if (lines[i].len == 0) continue;
    const size_t len = utf8SafeTruncateBuffer(definition.data() + lines[i].start,
                                              std::min(static_cast<size_t>(lines[i].len), MAX_LINE_BYTES));
    memcpy(buf, definition.data() + lines[i].start, len);
    buf[len] = '\0';
    renderer.drawText(fontId, x, startY + (i - firstLine) * lineHeight, buf, true,
                      lines[i].bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
}
void* DefinitionPayload::selectionChild(GfxRenderer& renderer, MappedInputManager& input,
                                        const XpDefinitionSelectionRequest& request, const XpUiRect body) {
  const auto& content = slots[0];
  std::span<const std::unique_ptr<Page>> pages = selectionPages;
  if (!content.pages.empty()) {
    pages = std::span(content.pages)
                .subspan(request.page, std::min<size_t>(SELECTION_PAGE_WINDOW, content.pages.size() - request.page));
  }
  const auto& s = request.selection;
  const ClipSelectionActivity::Selection selection{s.start_page, s.end_page, s.start_word, s.end_word, s.valid != 0};
  auto child =
      makeUniqueNoThrow<ClipSelectionActivity>(renderer, input, pages, body.x, body.y, selection, request.x, request.y);
  if (!child) {
    LOG_ERR("DICT", "OOM: definition selection (%u bytes)", static_cast<unsigned>(sizeof(ClipSelectionActivity)));
    return nullptr;
  }
  return ownLegacy(std::move(child)).release();
}
void DefinitionPayload::drawHeader(GfxRenderer& renderer, XpUiRect bounds, int page, int pages) const {
  char counter[16]{};
  if (pages > 1) snprintf(counter, sizeof(counter), "%d/%d", page + 1, pages);
  GUI.drawHeader(renderer, Rect{bounds.x, bounds.y, bounds.w, bounds.h}, slots[0].headword.c_str(),
                 counter[0] ? counter : nullptr);
}

uint8_t DefinitionPayload::constructPlain(XpDefinitionPayloadStorage* storage, XpAppText title, uint32_t length,
                                          XpEpubDefinitionFill fill, void* context) {
  std::string text;
  text.resize(length);
  if (!fill(context, reinterpret_cast<uint8_t*>(text.data()), length)) return 0;
  auto* payload = ::new (storage) DefinitionPayload;
  payload->slots[0].headword.assign(title.len ? reinterpret_cast<const char*>(title.data) : "", title.len);
  payload->slots[0].text = std::move(text);
  return 1;
}
uint8_t DefinitionPayload::constructNative(XpDefinitionPayloadStorage* storage, XpDefinitionNativeTransfer* transfer,
                                           uint8_t* html) {
  auto* payload = ::new (storage) DefinitionPayload;
  payload->slots[0].headword = *transfer->title;
  switch (transfer->kind) {
    case XpDefinitionNativeTransfer::TextKind::Copy:
      payload->slots[0].text = *transfer->text.copy;
      break;
    case XpDefinitionNativeTransfer::TextKind::Move:
      payload->slots[0].text = std::move(*transfer->text.move);
      break;
  }
  *html = transfer->html;
  return 1;
}
