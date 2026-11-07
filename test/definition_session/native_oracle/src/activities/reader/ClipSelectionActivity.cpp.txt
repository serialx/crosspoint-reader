#include "ClipSelectionActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "DictionaryUiBindings.h"
#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "clippings/ClippingText.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "util/WordSelectionInput.h"

using Input = WordSelectionInput;

namespace {

const char* cleanWordStart(const char* text) {
  if (!text) return "";
  if (clippingText::hasEmSpacePrefix(text)) text += 3;
  while (*text != '\0' && (*text == ' ' || *text == '\r' || *text == '\n' || *text == '\t' ||
                           (static_cast<uint8_t>(text[0]) == 0xC2 && static_cast<uint8_t>(text[1]) == 0xA0))) {
    text += static_cast<uint8_t>(text[0]) == 0xC2 ? 2 : 1;
  }
  return text;
}

}  // namespace

ClipSelectionActivity::ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::vector<std::unique_ptr<Page>> pages, const int marginLeft,
                                             const int marginTop, const int initialX, const int initialY,
                                             const Mode mode)
    : RustActivityAdapter("ClipSelection", renderer, mappedInput),
      pages(std::move(pages)),
      marginLeft(marginLeft),
      marginTop(marginTop),
      initialX(initialX),
      initialY(initialY),
      mode(mode) {}

ClipSelectionActivity::ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::span<const std::unique_ptr<Page>> borrowedPages, const int marginLeft,
                                             const int marginTop, const Selection selection, const int initialX,
                                             const int initialY)
    : RustActivityAdapter("ClipSelection", renderer, mappedInput),
      borrowedPages(borrowedPages),
      initialSelection(selection),
      marginLeft(marginLeft),
      marginTop(marginTop),
      initialX(initialX),
      initialY(initialY),
      mode(Mode::DefinitionLookup) {}

void ClipSelectionActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  lineHeight = renderer.getLineHeight(fontId);
  const bool extracted = extractWords();
  if (!extracted && mode == Mode::DefinitionLookup) {
    static constexpr StrId OPTIONS[] = {StrId::STR_BACK};
    actionPopup.show(StrId::STR_DICT_LOW_MEMORY, OPTIONS, 1, 0, [this](int) { cancel(); });
    requestUpdate();
    return;
  }
  if (!extracted || wordCount == 0) {
    if (wordCount == 0) LOG_ERR("CLIP", "No selectable words on current page");
    cancel();
    return;
  }
  int first = -1, last = -1;
  if (initialSelection.valid) {
    for (size_t i = 0; i < wordCount; ++i) {
      const auto& word = words[i];
      if (word.pageOffset == initialSelection.firstPage && word.pageWordIndex == initialSelection.firstWord) first = i;
      if (word.pageOffset == initialSelection.lastPage && word.pageWordIndex == initialSelection.lastWord) last = i;
    }
  }
  xp_ui_selection_init(&state, static_cast<uint8_t>(mode), wireWords(), wordCount, renderer.getScreenWidth(), first,
                       last);
  if (initialX >= 0) {
    state.range_start = -1;
    XpUiSelectionInput input{};
    input.input.touch_x = initialX;
    input.input.touch_y = initialY;
    input.tap = 1;
    layout = sampleLayout();
    applySelection(xp_ui_selection_update(&state, wireWords(), wordCount, &layout, &input));
    state.ignore_touch = 1;
  }
  requestUpdate();
}

bool ClipSelectionActivity::extractWords() {
  wordCount = 0;
  size_t capacity = MAX_SELECTABLE_WORDS;
  if (mode == Mode::DefinitionLookup) {
    capacity = 0;
    for (size_t i = 0; i < pageCount(); ++i) {
      for (uint16_t index = 0; index < pageAt(i).elementCount(); ++index) {
        const auto element = pageAt(i).element(index);
        if (element.getTag() == TAG_PageLine) {
          const auto& textBlock = element.textBlock();
          const auto* block = &textBlock;
          if (block->valid()) capacity += block->wordCount();
        }
      }
    }
    const size_t bytes = capacity * sizeof(WordBox);
    const auto heap = HalMemory::getDefaultHeap();
    if (capacity > UINT16_MAX || heap.largestBlockBytes < bytes || heap.freeBytes < bytes + 8192) {
      LOG_ERR("DICT", "Low heap for selection (%u bytes)", static_cast<unsigned>(bytes));
      return false;
    }
  }
  words = makeUniqueNoThrow<WordBox[]>(capacity);
  if (!words) {
    LOG_ERR("CLIP", "OOM: selection words (%u bytes)", static_cast<unsigned>(capacity * sizeof(WordBox)));
    return false;
  }
  rowCount = 0;
  const bool needsFontPrewarm = renderer.isSdCardFont(fontId);
  bool paragraphStartPending = false;

  for (size_t pageOffset = 0; pageOffset < pageCount(); ++pageOffset) {
    uint16_t pageWordIndex = 0;
    for (uint16_t index = 0; index < pageAt(pageOffset).elementCount(); ++index) {
      const auto line = pageAt(pageOffset).element(index);
      if (line.getTag() != TAG_PageLine) continue;
      const auto& textBlock = line.textBlock();
      const auto* block = &textBlock;
      if (!block->valid()) continue;

      if (needsFontPrewarm && block->wordCount() != 0) {
        uint8_t styleMask = 0;
        for (uint16_t i = 0; i < block->wordCount(); ++i) {
          styleMask |= static_cast<uint8_t>(1U << (static_cast<uint8_t>(block->wordStyle(i)) & 0x03));
        }
        // Borrow the line's packed words to prepare advances before measuring, without copying or truncating text.
        const char* text = block->wordText(0);
        const uint16_t lastWord = block->wordCount() - 1;
        const size_t textLength = block->wordText(lastWord) + block->wordTextLen(lastWord) + 1 - text;
        renderer.ensureSdCardFontReady(fontId, &text, &textLength, 1, false, false, styleMask);
      }

      const size_t lineStart = wordCount;
      const bool isRtl = block->getBlockStyle().isRtl;
      const int8_t characterSpacing = block->getBlockStyle().characterSpacing;
      const size_t remaining = capacity - lineStart;
      size_t rtlWordCount = 0;
      const int lineFont = TextBlock::renderFontId(renderer, block->blockFontId(fontId), fontId);
      const int rubyShift = block->getRubyShift(renderer.getFontAscenderSize(lineFont));
      for (uint16_t i = 0; i < block->wordCount(); ++i) {
        if (block->wordStartsParagraph(i)) paragraphStartPending = true;
        const char* text = block->wordText(i);
        if (!clippingText::hasVisibleText(text)) continue;

        const auto style = static_cast<EpdFontFamily::Style>(block->wordStyle(i) & ~EpdFontFamily::UNDERLINE);
        const int wordFont = TextBlock::renderFontId(renderer, block->wordFontId(i, fontId), fontId);
        int width = renderer.getTextAdvanceX(wordFont, text, style, characterSpacing);
        if (width <= 0) continue;
        if (i + 1 < block->wordCount() && block->wordXpos(i + 1) > block->wordXpos(i)) {
          width = std::min(width, static_cast<int>(block->wordXpos(i + 1) - block->wordXpos(i)));
        }

        if (!isRtl && wordCount == capacity) break;

        WordBox& word = isRtl ? words[lineStart + (rtlWordCount < remaining ? rtlWordCount : rtlWordCount % remaining)]
                              : words[wordCount++];
        word.x = static_cast<int16_t>(marginLeft + line.xPos + block->wordXpos(i));
        word.y = static_cast<int16_t>(marginTop + line.yPos + rubyShift + block->wordYOffset(renderer, fontId, i));
        word.width = static_cast<int16_t>(width);
        word.height = static_cast<int16_t>(renderer.getLineHeight(wordFont));
        word.fontId = wordFont;
        word.row = rowCount;
        word.pageOffset = static_cast<uint8_t>(pageOffset);
        word.pageWordIndex = pageWordIndex++;
        word.startOffset = block->wordSourceRange(i).start;
        word.endOffset = block->wordSourceRange(i).end;
        word.text = text;
        word.style = static_cast<uint8_t>(style);
        word.characterSpacing = characterSpacing;
        word.paragraphStart = false;
        word.isRtl = isRtl;
        word.discretionaryHyphen = block->wordHasDiscretionaryHyphen(i);
        if (isRtl) ++rtlWordCount;
      }
      if (isRtl) {
        const size_t stored = std::min(remaining, rtlWordCount);
        wordCount = lineStart + stored;
        if (rtlWordCount > remaining) {
          std::rotate(words.get() + lineStart, words.get() + lineStart + rtlWordCount % remaining,
                      words.get() + wordCount);
        }
        std::reverse(words.get() + lineStart, words.get() + wordCount);
      }
      if (wordCount > lineStart && paragraphStartPending) {
        // Whitespace-only tokens are skipped; keep their boundary on the first selectable logical word.
        const auto first =
            std::min_element(words.get() + lineStart, words.get() + wordCount,
                             [](const WordBox& a, const WordBox& b) { return a.startOffset < b.startOffset; });
        first->paragraphStart = true;
        paragraphStartPending = false;
      }
      if (wordCount != lineStart) ++rowCount;
      if (wordCount == capacity) {
        if (mode != Mode::DefinitionLookup) {
          LOG_ERR("CLIP", "Selectable word cap hit (%u); multi-page selection was truncated",
                  static_cast<unsigned>(capacity));
        }
        break;
      }
    }
    if (wordCount == capacity) break;
  }

  return true;
}

bool ClipSelectionActivity::buildSelectedText(const int first, const int last, std::string& text) const {
  text.clear();
  text.reserve(CLIPPING_TEXT_MAX);
  const size_t count = last - first + 1;
  auto order = makeUniqueNoThrow<uint16_t[]>(count);
  if (!order) {
    LOG_ERR("CLIP", "OOM: selection export order");
    return false;
  }
  bool allSourcesKnown = true;
  for (size_t i = 0; i < count; ++i) {
    order[i] = static_cast<uint16_t>(first + i);
    allSourcesKnown &= words[order[i]].startOffset != UINT32_MAX && words[order[i]].endOffset != UINT32_MAX;
  }
  // Keep navigation in visual order and export in logical source order.
  if (allSourcesKnown) {
    std::sort(order.get(), order.get() + count, [this](const uint16_t a, const uint16_t b) {
      if (words[a].startOffset == words[b].startOffset) return a < b;
      return words[a].startOffset < words[b].startOffset;
    });
  }
  for (size_t i = 0; i < count; ++i) {
    const WordBox& current = words[order[i]];
    const char* word = cleanWordStart(current.text);
    char separator = '\0';
    if (i > 0) {
      const WordBox& previous = words[order[i - 1]];
      const bool sourceKnown = previous.endOffset != UINT32_MAX && current.startOffset != UINT32_MAX;
      const bool separated = sourceKnown ? current.startOffset > previous.endOffset
                                         : current.row != previous.row || current.x > previous.x + previous.width + 2;
      if (current.paragraphStart) {
        separator = '\n';
      } else if (separated) {
        separator = ' ';
      }
    }
    if (!clippingText::append(text, word, separator, CLIPPING_TEXT_MAX, current.discretionaryHyphen)) {
      return false;
    }
  }
  return true;
}

void ClipSelectionActivity::confirmSelection(const ClippingResult::Action action) {
  if (state.range_start < 0) {
    state.range_start = state.selected;
    requestUpdate();
    return;
  }

  const int first = std::min(state.range_start, state.selected);
  const int last = std::max(state.range_start, state.selected);
  ClippingResult result;
  result.action = action;
  const auto heap = HalMemory::getDefaultHeap();
  // reserve() aborts on OOM; leave room for the sort order and error popup too.
  const size_t requiredHeap = CLIPPING_TEXT_MAX + 1 + (last - first + 1) * sizeof(uint16_t) + 1024;
  const bool lowMemory = heap.largestBlockBytes < CLIPPING_TEXT_MAX + 1 || heap.freeBytes < requiredHeap;
  if (lowMemory || !buildSelectedText(first, last, result.text)) {
    static constexpr StrId OPTIONS[] = {StrId::STR_BACK};
    actionPopup.show(lowMemory ? StrId::STR_MEMORY_ERROR : StrId::STR_CLIPPING_TOO_LONG, OPTIONS, 1, 0, [](int) {});
    requestUpdate();
    return;
  }
  result.startPageOffset = words[first].pageOffset;
  result.endPageOffset = words[last].pageOffset;
  result.startWordIndex = words[first].pageWordIndex;
  result.endWordIndex = words[last].pageWordIndex;
  result.wordCount = static_cast<uint16_t>(last - first + 1);
  result.startOffset = UINT32_MAX;
  result.endOffset = 0;
  for (int i = first; i <= last; ++i) {
    if (words[i].startOffset == UINT32_MAX || words[i].endOffset == UINT32_MAX) {
      result.startOffset = result.endOffset = UINT32_MAX;
      break;
    }
    result.startOffset = std::min(result.startOffset, words[i].startOffset);
    result.endOffset = std::max(result.endOffset, words[i].endOffset);
  }
  setResult(std::move(result));
  finish();
}

void ClipSelectionActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

bool ClipSelectionActivity::handleHomeGesture() {
  cancel();
  return true;
}

void ClipSelectionActivity::loopButtons() {
  const auto input = dictionaryUiInput(mappedInput);
  bool queued;
  {
    ModelLock lock(*this);
    queued = xp_ui_selection_poll(&state, &input);
  }
  if (!queued) LOG_ERR("CLIP", "Selection button queue full");
  RenderLock lock(RenderLock::Mode::Try);
  if (!lock.ownsLock()) return;
  if (rustSceneRejected()) {
    consumeRejectedInput();
    return;
  }
  for (;;) {
    uint8_t buttons;
    {
      ModelLock model(*this);
      buttons = xp_ui_selection_dequeue(&state);
    }
    if (!buttons || handleButtons(buttons)) break;
  }
}

void ClipSelectionActivity::drainRejectedTouch() {
  int x = 0, y = 0;
  if (!mappedInput.isScreenTouchHeld(x, y)) {
    ModelLock lock(*this);
    state.ignore_touch = 0;
  }
}

void ClipSelectionActivity::loop() {
  if (consumeRejectedInput()) {
    drainRejectedTouch();
    return;
  }
  if (wordCount == 0 && !actionPopup.isActive()) {
    cancel();
    return;
  }
  if (!mappedInput.hasTouch()) {
    loopButtons();
    return;
  }
  RenderLock lock;
  if (rustSceneRejected()) {
    consumeRejectedInput();
    drainRejectedTouch();
    return;
  }
  if (actionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  XpUiSelectionInput input{};
  input.input = dictionaryUiInput(mappedInput);
  int x = 0, y = 0;
  if (state.ignore_touch) {
    input.held = mappedInput.isScreenTouchHeld(x, y);
  } else if (!state.dragging && mappedInput.wasScreenTapped(x, y)) {
    input.tap = 1;
  } else if (state.dragging) {
    input.held = mappedInput.isScreenTouchHeld(x, y);
  } else {
    input.pressed = mappedInput.wasScreenTouchPressed(x, y);
  }
  input.input.touch_x = x;
  input.input.touch_y = y;
  applySelection(xp_ui_selection_update(&state, wireWords(), wordCount, &layout, &input));
}

bool ClipSelectionActivity::handleButtons(const uint8_t buttons) {
  if (actionPopup.handleButtons(buttons & Input::INPUT_PREVIOUS, buttons & Input::INPUT_NEXT,
                                buttons & Input::INPUT_CONFIRM, buttons & Input::INPUT_BACK,
                                [this] { requestUpdate(); }))
    return true;
  applySelection(xp_ui_selection_buttons(&state, wireWords(), wordCount, buttons));
  return buttons & (Input::INPUT_BACK | Input::INPUT_CONFIRM);
}

void ClipSelectionActivity::applySelection(const XpUiSelectionUpdate& effect) {
  if (effect.invalidate) requestUpdate();
  switch (static_cast<XpUiSelectionEffect>(effect.kind)) {
    case XP_UI_SELECT_IDLE:
      break;
    case XP_UI_SELECT_CANCEL:
      cancel();
      break;
    case XP_UI_SELECT_CONFIRM:
      confirmSelection(static_cast<ClippingResult::Action>(effect.action));
      break;
    case XP_UI_SELECT_QUEUE_FULL:
      LOG_ERR("CLIP", "Selection button queue full");
      break;
  }
}

void ClipSelectionActivity::render(RenderLock&&) {
  if (actionPopup.processRender(renderer, mappedInput)) {
    painted.valid = 0;
    return;
  }
  {
    ModelLock lock(*this);
    rendered = state;
  }
  layout = sampleLayout();
  const XpUiBuffer buffer{commands, nullptr, 20, 0};
  XpUiScene scene{};
  if (xp_ui_selection_view(&rendered, wireWords(), wordCount, &layout, &painted, &buffer, &nextPainted, &scene) != 1) {
    rejectRustScene();
    LOG_ERR("RUI", "Cannot build scene in %s", name.c_str());
    return;
  }
  const bool complete = paintRustScene(scene);
  framePrewarm.reset();
  if (complete) painted = nextPainted;
}

XpUiSelectionLayout ClipSelectionActivity::sampleLayout() const {
  const auto& m = UITheme::getInstance().getMetrics();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int font = uiScaleSpec().smallFontId;
  // Measuring action labels loads their font; button-only selection never needs it.
  const bool actions = mappedInput.hasTouch() && state.range_start >= 0;
  const int width =
      actions ? std::max({renderer.getTextWidth(font, tr(STR_LOOKUP)), renderer.getTextWidth(font, tr(STR_CLIP)),
                          renderer.getTextWidth(font, tr(STR_BOOKMARK_OPTION))})
              : 0;
  return {{0, 0, static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(renderer.getScreenHeight())},
          {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y), static_cast<int16_t>(safe.width),
           static_cast<int16_t>(safe.height)},
          static_cast<int16_t>(top),
          static_cast<int16_t>(right),
          static_cast<int16_t>(bottom),
          static_cast<int16_t>(left),
          static_cast<int16_t>(m.verticalSpacing),
          static_cast<int16_t>(m.menuSpacing),
          static_cast<int16_t>(m.topPadding),
          static_cast<int16_t>(lineHeight),
          static_cast<int16_t>(width),
          static_cast<int16_t>(actions ? renderer.getLineHeight(font) : 0),
          font,
          static_cast<uint8_t>(mappedInput.hasTouch()),
          static_cast<uint8_t>(m.controlRadius),
          {0, 0}};
}

XpUiPaint ClipSelectionActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void*, uint32_t handle, uint32_t generation) -> uint8_t {
            return generation == 1 &&
                   (handle == XP_UI_DICT_PAGE || handle == XP_UI_DICT_SELECTION || handle == XP_UI_DICT_FOOTER ||
                    (handle >= XP_UI_DICT_LOOKUP && handle <= XP_UI_DICT_BOOKMARK));
          },
          [](void* context, const XpGlyphTarget* target, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<ClipSelectionActivity*>(context);
            auto& renderer = self.renderer;
            const auto previous = renderer.getClipRect();
            // The shared footer temporarily rotates to portrait and retains the caller's clip.
            if (command->resource != XP_UI_DICT_FOOTER) renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
            switch (command->resource) {
              case XP_UI_DICT_PAGE: {
                self.framePrewarm.emplace(*renderer.getFontCacheManager());
                self.pageAt(self.rendered.page)
                    .render(renderer, self.fontId, self.marginLeft + self.nextPainted.offset_x,
                            self.marginTop + self.nextPainted.offset_y);
                self.framePrewarm->endScanAndPrewarm();
                self.pageAt(self.rendered.page)
                    .render(renderer, self.fontId, self.marginLeft + self.nextPainted.offset_x,
                            self.marginTop + self.nextPainted.offset_y);
                break;
              }
              case XP_UI_DICT_SELECTION: {
                struct Context {
                  ClipSelectionActivity* self;
                  bool incremental;
                } fontsContext{&self, command->style != 0};
                const XpUiSelectionFonts fonts{
                    &fontsContext,
                    [](void* context, const XpGlyphTarget*, const XpUiSelectionWord* word, int32_t x, int32_t y) {
                      auto& ctx = *static_cast<Context*>(context);
                      auto& renderer = ctx.self->renderer;
                      if (ctx.incremental)
                        renderer.getFontCacheManager()->prewarmCache(word->fontId, word->text, 1u << (word->style & 3));
                      renderer.drawText(word->fontId, x, y, word->text, true,
                                        static_cast<EpdFontFamily::Style>(word->style), BidiUtils::BidiBaseDir::AUTO,
                                        word->characterSpacing);
                    }};
                auto band = *target;
                band.clip = {clip.x, clip.y, clip.x + clip.w, clip.y + clip.h};
                if (!xp_ui_selection_paint(&self.rendered, self.wireWords(), self.wordCount, &self.layout,
                                           &self.painted, &band, &fonts, command->style))
                  LOG_ERR("RUI", "Invalid selection span");
                break;
              }
              case XP_UI_DICT_FOOTER: {
                const auto labels = self.mappedInput.mapLabels(
                    tr(STR_BACK),
                    self.rendered.range_start < 0 ? tr(STR_SELECT) : (self.isLookup() ? tr(STR_LOOKUP) : tr(STR_DONE)),
                    tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
                GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
                break;
              }
              default: {
                const char* labels[] = {tr(STR_LOOKUP), tr(STR_CLIP), tr(STR_BOOKMARK_OPTION)};
                renderer.drawUiText(*command, clip, labels[command->resource - XP_UI_DICT_LOOKUP]);
                break;
              }
            }
            renderer.setClipRect(previous[0], previous[1], previous[2], previous[3]);
          }};
}
