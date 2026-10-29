#include "DictionaryDefinitionActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>

#include "CrossPointSettings.h"
#include "DictionaryUiBindings.h"
#include "HapticFeedback.h"
#include "ReaderUtils.h"
#include "components/HeaderBackTapTarget.h"
#include "components/UITheme.h"
#include "util/DictHtmlPages.h"
#include "util/HtmlToPlainText.h"

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
struct DictionaryDefinitionActivity::PlainSelectionLine {
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

  void layout(const DictionaryDefinitionActivity& owner, const int fontId, const uint32_t offset) {
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
        x += right == left + 1 ? owner.measureSpan(fontId, text.c_str() + ranges[left].end - offset,
                                                   ranges[right].start - ranges[left].end)
                               : owner.renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
      }
      positions.push_back(x);
      x += owner.renderer.getTextAdvanceX(fontId, words[index].c_str(), EpdFontFamily::REGULAR);
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

// cppcheck-suppress constParameterReference // the lookup state holds the dictionary by non-const reference
bool DictionaryDefinitionActivity::enableLookup(Dictionary& dictionary, std::string&& query) {
  lookup = makeUniqueNoThrow<LookupState>(dictionary);
  if (!lookup) LOG_ERR("DICT", "OOM: lookup history (%u bytes)", static_cast<unsigned>(sizeof(LookupState)));
  if (lookup) lookup->query = std::move(query);
  return lookup != nullptr;
}

void DictionaryDefinitionActivity::onEnter() {
  Activity::onEnter();
  initialLayoutFailed = !prepareDefinition(content);
  if (initialLayoutFailed) {
    content.lines.clear();
    showMessage(StrId::STR_DICT_LOW_MEMORY);
  }
  requestUpdate();
}

bool DictionaryDefinitionActivity::prepareDefinition(Definition& next, const bool forcePlain) {
  std::replace(next.text.begin(), next.text.end(), '\0', '\n');
  if (htmlDefinition && !forcePlain && next.text.size() <= MAX_STYLED_HTML_BYTES && layoutHtmlPages(next)) return true;
  if (htmlDefinition) {
    if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
    const auto heap = HalMemory::getDefaultHeap();
    if (heap.largestBlockBytes < next.text.size() + 1 || heap.freeBytes < next.text.size() + HEAP_HEADROOM) {
      LOG_ERR("DICT", "Low heap for plain definition (%u bytes)", static_cast<unsigned>(next.text.size() + 1));
      return false;
    }
    next.text = htmlToPlainText(next.text, DICTIONARY_PLAIN_HEADWORD_MARKER);
  }
  return wrapText(next);
}

void DictionaryDefinitionActivity::onExit() {
  Activity::onExit();
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
  }
}

DictionaryDefinitionActivity::BodyArea DictionaryDefinitionActivity::bodyArea() const {
  const auto layout = dictionaryUiDefinitionLayout(renderer);
  const auto body = xp_ui_definition_body(&layout);
  return {body.x, body.y, body.w, body.h};
}

// Styled path: lay the HTML definition out through the EPUB chapter parser
// into reader-identical Pages. Frees `definition` on success (the page arenas
// own the text); any failure leaves state untouched for the plain-text path.
bool DictionaryDefinitionActivity::layoutHtmlPages(Definition& next) {
  auto& definition = next.text;
  auto& pages = next.pages;
  const BodyArea body = bodyArea();
  if (body.width <= 0 || body.height <= 0) {
    LOG_ERR("DHTML", "Invalid definition viewport: %dx%d", body.width, body.height);
    return false;
  }
  if (!buildDictionaryHtmlPages(renderer, definition, static_cast<uint16_t>(body.width),
                                static_cast<uint16_t>(body.height), pages)) {
    return false;
  }
  definition.clear();
  definition.shrink_to_fit();
  next.totalPages = static_cast<int>(pages.size());
  next.currentPage = 0;
  return true;
}

int DictionaryDefinitionActivity::measureSpan(const int fontId, const char* text, size_t len,
                                              const EpdFontFamily::Style family) const {
  char buf[MAX_LINE_BYTES + 1];
  len = std::min(len, MAX_LINE_BYTES);
  memcpy(buf, text, len);
  buf[len] = '\0';
  return renderer.getTextAdvanceX(fontId, buf, family);
}

bool DictionaryDefinitionActivity::reserveLines(Definition& next, const size_t count) {
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

bool DictionaryDefinitionActivity::wrapText(Definition& next) {
  auto& definition = next.text;
  auto& lines = next.lines;
  lines.clear();
  if (!reserveLines(next, definition.size() / 32 + 8)) return false;

  const int fontId = SETTINGS.getReaderFontId();
  // SD-card fonts: merge every definition codepoint into the persistent
  // advance table up front. Otherwise each unseen codepoint measured below
  // falls back to an on-demand glyph load from SD (8-slot overflow ring).
  renderer.ensureSdCardFontReady(fontId, definition.c_str(), 0x03 /* REGULAR + BOLD */);

  const BodyArea body = bodyArea();
  const int maxWidth = body.width;
  const int spaceWidth = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
  const int lineHeight = renderer.getLineHeight(fontId, PLAIN_TEXT_LINE_SPACING);
  next.linesPerPage = std::max(1, body.height / lineHeight);

  const char* text = definition.c_str();
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
    const int tokenWidth =
        measureSpan(fontId, text + tokenStart, tokenLen, lineBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

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
          if (measureSpan(fontId, text + lineStart, f, lineBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR) >
              maxWidth)
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
      lineWidth = measureSpan(fontId, text + lineStart, lineEnd - lineStart,
                              lineBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
  }
  if (lineEnd > lineStart && !flushLine(n)) return false;

  // Trim trailing blank lines so the last page is not empty padding.
  while (!lines.empty() && lines.back().len == 0) lines.pop_back();

  next.totalPages = std::max(1, (static_cast<int>(lines.size()) + next.linesPerPage - 1) / next.linesPerPage);
  next.currentPage = 0;
  return true;
}

void DictionaryDefinitionActivity::showMessage(const StrId nextMessage) {
  message = nextMessage;
  messageTime = millis();
  messageVisible = true;
  requestUpdate();
}

bool DictionaryDefinitionActivity::restoreCurrentDefinition(const bool styled) {
  const int page = content.currentPage;
  auto result = Dictionary::LookupResult::Found;
  if (!lookup->dictionary.lookup(lookup->query.c_str(), content.text, content.headword, &result) ||
      !prepareDefinition(content, !styled) || styled != !content.pages.empty() || page >= content.totalPages) {
    LOG_ERR("DICT", "Cannot recover current definition after failed lookup");
    finish();
    return false;
  }
  content.currentPage = page;
  return true;
}

void DictionaryDefinitionActivity::navigateDefinition(const bool back) {
  {
    RenderLock lock;
    if (!back && rustSceneRejected()) {
      lookup->pendingWord.clear();
      requestUpdate();
      return;
    }
    showMessage(StrId::STR_DICT_LOOKING_UP);
  }
  requestUpdateAndWait();

  RenderLock lock;
  if (!back && rustSceneRejected()) {
    lookup->pendingWord.clear();
    requestUpdate();
    return;
  }
  messageVisible = false;
  const bool styled = !content.pages.empty();
  // Keep only navigation metadata while inflating and laying out another entry.
  content.releasePayload();
  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
  const auto heap = HalMemory::getInternalHeap();
  LOG_DBG("DICT", "Navigation after release: free=%u largest=%u", static_cast<unsigned>(heap.freeBytes),
          static_cast<unsigned>(heap.largestBlockBytes));
  Definition next;
  auto result = Dictionary::LookupResult::Found;
  const char* word = back ? lookup->history[lookup->depth - 1].query.c_str() : lookup->pendingWord.c_str();
  const bool found = lookup->dictionary.lookup(word, next.text, next.headword, &result);
  const auto recover = [this, back, styled, &next](const StrId error) {
    lookup->pendingWord.clear();
    next.releasePayload();
    std::string{}.swap(next.headword);
    if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
    if (back) {
      LOG_ERR("DICT", "Cannot restore previous definition; leaving lookup");
      finish();
    } else if (restoreCurrentDefinition(styled)) {
      showMessage(error);
    }
  };
  if (!found) {
    switch (result) {
      case Dictionary::LookupResult::Found:
      case Dictionary::LookupResult::ReadError:
        recover(StrId::STR_DICT_READ_FAILED);
        break;
      case Dictionary::LookupResult::NotFound:
        recover(StrId::STR_DICT_NOT_FOUND);
        break;
      case Dictionary::LookupResult::LowMemory:
        recover(StrId::STR_DICT_LOW_MEMORY);
        break;
      case Dictionary::LookupResult::Decompress:
        recover(StrId::STR_DICT_DECOMPRESS_ERROR);
        break;
    }
    return;
  }
  const bool forcePlain = back && !lookup->history[lookup->depth - 1].styled;
  if (!prepareDefinition(next, forcePlain)) {
    recover(StrId::STR_DICT_LOW_MEMORY);
    return;
  }
  if (back) {
    auto& previous = lookup->history[lookup->depth - 1];
    if (previous.styled != !next.pages.empty() || previous.page >= next.totalPages) {
      LOG_ERR("DICT", "Cannot restore definition layout");
      recover(StrId::STR_DICT_RESTORE_FAILED);
      return;
    }
    lookup->query = std::move(previous.query);
    next.currentPage = previous.page;
    next.selectionPage = previous.selectionPage;
    next.selection = previous.selection;
    --lookup->depth;
  } else {
    auto& previous = lookup->history[lookup->depth++];
    previous.query = std::move(lookup->query);
    lookup->query = std::move(lookup->pendingWord);
    previous.page = content.currentPage;
    previous.selectionPage = content.selectionPage;
    previous.selection = content.selection;
    previous.styled = styled;
  }
  lookup->pendingWord.clear();
  content = std::move(next);
  requestUpdate();
}

bool DictionaryDefinitionActivity::buildPlainSelectionPages() {
  // Only the selection window gets packed word arenas; the definition keeps its cheap line spans.
  const int fontId = SETTINGS.getReaderFontId();
  const int lineHeight = renderer.getLineHeight(fontId, PLAIN_TEXT_LINE_SPACING);
  const int pageCount = std::min<int>(SELECTION_PAGE_WINDOW, content.totalPages - content.selectionPage);
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
  lookup->selectionPages.reserve(SELECTION_PAGE_WINDOW);
  size_t capacity = LinePageBuilder::capacityForLines(0, 0, 0);
  for (int p = 0; p < pageCount; ++p) {
    size_t lines = 0, words = 0, textBytes = 0;
    const int first = (content.selectionPage + p) * content.linesPerPage;
    const int last = std::min(first + content.linesPerPage, static_cast<int>(content.lines.size()));
    for (int row = first; row < last; ++row) {
      const auto& line = content.lines[row];
      const char* text = content.text.c_str() + line.start;
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
    const int firstLine = (content.selectionPage + p) * content.linesPerPage;
    const int lastLine = std::min(firstLine + content.linesPerPage, static_cast<int>(content.lines.size()));
    heap = HalMemory::getDefaultHeap();
    const size_t pageBytes = Page::plainLineAllocationSize(capacity, lastLine - firstLine);
    if (pageBytes > SIZE_MAX - HEAP_HEADROOM) return false;
    if (heap.freeBytes < pageBytes + HEAP_HEADROOM || heap.largestBlockBytes < pageBytes) return false;
    for (int row = firstLine; row < lastLine; ++row) {
      const auto& line = content.lines[row];
      if (!line.len) continue;
      const size_t length =
          utf8SafeTruncateBuffer(content.text.c_str() + line.start, std::min<size_t>(line.len, MAX_LINE_BYTES));
      scratch->text.assign(content.text.c_str() + line.start, length);
      heap = HalMemory::getDefaultHeap();
      const size_t lineBytes = length * (sizeof(TextBlock::SourceRange) + 12) + sizeof(TextBlock) + 1024;
      if (heap.freeBytes < lineBytes + HEAP_HEADROOM || heap.largestBlockBytes < lineBytes) return false;
      scratch->layout(*this, fontId, line.start);
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
    lookup->selectionPages.push_back(std::move(page));
  }
  return true;
}

void DictionaryDefinitionActivity::startSelection(const int x, const int y) {
  RenderLock lock;
  if (rustSceneRejected()) {
    requestUpdate();
    return;
  }
  if (!content.selection.valid || x >= 0) {
    content.selectionPage = content.currentPage;
    content.selection.valid = false;
  }
  std::span<const std::unique_ptr<Page>> pages;
  if (content.pages.empty()) {
    if (!buildPlainSelectionPages()) {
      LOG_ERR("DICT", "Cannot allocate plain definition selection");
      lookup->selectionPages.clear();
      showMessage(StrId::STR_DICT_LOW_MEMORY);
      return;
    }
    pages = lookup->selectionPages;
  } else {
    pages = std::span(content.pages)
                .subspan(content.selectionPage,
                         std::min<size_t>(SELECTION_PAGE_WINDOW, content.pages.size() - content.selectionPage));
  }
  const auto body = bodyArea();
  auto selection =
      makeUniqueNoThrow<ClipSelectionActivity>(renderer, mappedInput, pages, body.x, body.y, content.selection, x, y);
  if (!selection) {
    LOG_ERR("DICT", "OOM: definition selection (%u bytes)", static_cast<unsigned>(sizeof(ClipSelectionActivity)));
    lookup->selectionPages.clear();
    showMessage(StrId::STR_DICT_LOW_MEMORY);
    return;
  }
  startActivityForResult(std::move(selection), [this](const ActivityResult& result) {
    RenderLock lock;
    lookup->selectionPages.clear();
    if (result.isCancelled) return;
    const auto& selected = std::get<ClippingResult>(result.data);
    content.currentPage = content.selectionPage + selected.endPageOffset;
    content.selection = {selected.startPageOffset, selected.endPageOffset, selected.startWordIndex,
                         selected.endWordIndex, true};
    if (lookup->depth == lookup->history.size()) {
      showMessage(StrId::STR_DICT_HISTORY_FULL);
      return;
    }
    const auto heap = HalMemory::getDefaultHeap();
    if (heap.largestBlockBytes < selected.text.size() + 1 || heap.freeBytes < selected.text.size() + HEAP_HEADROOM) {
      LOG_ERR("DICT", "Low heap for lookup text (%u bytes)", static_cast<unsigned>(selected.text.size() + 1));
      showMessage(StrId::STR_DICT_LOW_MEMORY);
      return;
    }
    lookup->pendingWord = selected.text;
  });
}

void DictionaryDefinitionActivity::loop() {
  bool discard;
  {
    ModelLock lock(*this);
    discard = discardPendingLookup;
    discardPendingLookup = false;
  }
  if (discard && lookup) lookup->pendingWord.clear();
  if (initialLayoutFailed && millis() - messageTime >= 1500) {
    finish();
    return;
  }
  if (consumeRejectedInput()) return;
  if (lookup && !lookup->pendingWord.empty() && !initialLayoutFailed) {
    navigateDefinition(false);
    return;
  }
  int x = 0, y = 0;
  const bool longPress = !initialLayoutFailed && lookup && mappedInput.wasScreenLongPress(x, y);
  const bool headerLongPress = longPress && HeaderBackTapTarget::contains(x, y);
  auto input = dictionaryUiInput(mappedInput);
  int tx = 0, ty = 0;
  const bool tap = !initialLayoutFailed && mappedInput.wasScreenTapped(tx, ty);
  input.touch_x = tap ? tx : x;
  input.touch_y = tap ? ty : y;
  XpUiEffect effect;
  {
    ModelLock lock(*this);
    state.page = std::atomic_ref(content.currentPage).load();
    state.pages = content.totalPages;
    state.lookup = lookup != nullptr;
    state.history_depth = lookup ? lookup->depth : 0;
    state.failed = initialLayoutFailed;
    state.message_visible = messageVisible;
    state.message_time = messageTime;
    effect = xp_ui_definition_update(&state, &input, tap, longPress, headerLongPress, renderer.getScreenWidth(),
                                     SETTINGS.touchReaderControls);
    std::atomic_ref(content.currentPage).store(state.page);
    if (effect.value) content.selection.valid = false;
  }
  // Message dismissal still waits for the current paint to finish.
  if (messageVisible && !state.message_visible) {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock())
      messageVisible = false;
    else
      effect.invalidate = effect.value != 0;
  }
  if (effect.invalidate) requestUpdate();
  if (tap && effect.value) haptic_feedback::touchAction();
  applyNavigation(effect, x, y);
}

void DictionaryDefinitionActivity::applyNavigation(const XpUiEffect& effect, int x, int y) {
  switch (static_cast<XpUiDefinitionEffect>(effect.kind)) {
    case XP_UI_DEFINITION_IDLE:
      break;
    case XP_UI_DEFINITION_FINISH:
      finish();
      break;
    case XP_UI_DEFINITION_CANCEL: {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      break;
    }
    case XP_UI_DEFINITION_BACK:
      navigateDefinition(true);
      break;
    case XP_UI_DEFINITION_SELECT:
      startSelection();
      break;
    case XP_UI_DEFINITION_TOUCH_SELECT:
      startSelection(x, y);
      break;
  }
}

// Draws the current page: a styled Page when the HTML layout succeeded,
// otherwise the wrapped line spans (copied into a stack buffer for NUL
// termination). Called twice per render: once in font-cache scan mode, once
// for the real paint.
void DictionaryDefinitionActivity::drawBody(const int fontId, const int x, const int startY,
                                            const int currentPage) const {
  const auto& pages = content.pages;
  const auto& lines = content.lines;
  const auto& definition = content.text;
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
    const size_t len = utf8SafeTruncateBuffer(definition.c_str() + lines[i].start,
                                              std::min(static_cast<size_t>(lines[i].len), MAX_LINE_BYTES));
    memcpy(buf, definition.c_str() + lines[i].start, len);
    buf[len] = '\0';
    renderer.drawText(fontId, x, startY + (i - firstLine) * lineHeight, buf, true,
                      lines[i].bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
}

void DictionaryDefinitionActivity::render(RenderLock&&) {
  rendered.page = std::atomic_ref(content.currentPage).load();
  rendered.pages = content.totalPages;
  rendered.lookup = lookup != nullptr;
  rendered.message_visible = messageVisible;
  const auto layout = dictionaryUiDefinitionLayout(renderer);
  const XpUiBuffer buffer{commands, nullptr, 4, 0};
  XpUiScene scene{};
  if (xp_ui_definition_view(&rendered, &layout, &buffer, &scene) != 1) {
    rejectRustScene();
    return;
  }
  const bool complete = paintRustScene(scene, !rendered.message_visible);
  if (complete && rendered.message_visible) {
    if (dictionaryUiPopup(renderer, I18N.get(message), commands, 4, scene))
      paintRustScene(scene);
    else
      rejectRustScene();
  }
  framePrewarm.reset();
}

XpUiPaint DictionaryDefinitionActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void*, uint32_t handle, uint32_t generation) -> uint8_t {
            return generation == 1 && handle >= XP_UI_DICT_FOOTER && handle <= XP_UI_DICT_MESSAGE;
          },
          [](void* context, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<DictionaryDefinitionActivity*>(context);
            auto& renderer = self.renderer;
            switch (command->resource) {
              case XP_UI_DICT_HEADER: {
                char counter[16]{};
                if (self.rendered.pages > 1)
                  snprintf(counter, sizeof(counter), "%d/%d", static_cast<int>(self.rendered.page + 1),
                           static_cast<int>(self.rendered.pages));
                const auto& r = command->rect;
                GUI.drawHeader(renderer, Rect{r.x, r.y, r.w, r.h}, self.content.headword.c_str(),
                               counter[0] ? counter : nullptr);
                break;
              }
              case XP_UI_DICT_BODY: {
                self.framePrewarm.emplace(*renderer.getFontCacheManager());
                self.drawBody(SETTINGS.getReaderFontId(), command->rect.x, command->rect.y, self.rendered.page);
                self.framePrewarm->endScanAndPrewarm();
                self.drawBody(SETTINGS.getReaderFontId(), command->rect.x, command->rect.y, self.rendered.page);
                break;
              }
              case XP_UI_DICT_FOOTER: {
                const auto labels = self.mappedInput.mapLabels(tr(STR_BACK), self.lookup ? tr(STR_LOOKUP) : "",
                                                               self.rendered.page > 0 ? "<" : "",
                                                               self.rendered.page + 1 < self.rendered.pages ? ">" : "");
                GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
                break;
              }
              case XP_UI_DICT_MESSAGE:
                renderer.drawUiText(*command, clip, I18N.get(self.message));
                break;
              default:
                break;
            }
          }};
}

void DictionaryDefinitionActivity::cancelRejectedInput(const XpUiInput& input) {
  if (initialLayoutFailed || !(input.released & XP_UI_BACK)) return;
  XpUiEffect effect{};
  effect.kind = lookup && input.held_ms >= 1000 ? XP_UI_DEFINITION_CANCEL
                : lookup && lookup->depth > 0   ? XP_UI_DEFINITION_BACK
                                                : XP_UI_DEFINITION_FINISH;
  applyNavigation(effect, 0, 0);
}
