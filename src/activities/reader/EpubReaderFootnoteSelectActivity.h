#pragma once

#include <Epub/Page.h>
#include <Epub/PageLink.h>
#include <I18n.h>
#include <xp_ui_reader_chrome.h>

#include <array>
#include <memory>

#include "activities/RustActivityAdapter.h"

// Footnote reference selection overlaid on the current reader page, modeled on
// DictionaryWordSelectActivity: the page renders underneath and each footnote
// reference marker gets a highlight box. Up/Down/Left/Right move between
// markers, Confirm jumps to that footnote, Back returns to the reader.
class EpubReaderFootnoteSelectActivity final : public RustActivityAdapter {
 public:
  explicit EpubReaderFootnoteSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                            std::unique_ptr<Page> page, int marginLeft, int marginTop)
      : RustActivityAdapter("EpubReaderFootnoteSelect", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop) {}

 private:
  void onEnter() override;
  XpUiInterest rustInterest(uint32_t now) const override { return xp_ui_reader_footnote_interest(&state, now); }
  XpUiEffect updateRust(const XpUiInput& input) override;
  void applyRustEffect(const XpUiEffect& effect) override;
  void render(RenderLock&&) override;

 private:
  void buildFootnoteLinks();
  void performJump(int index);
  bool drawHighlightWithSnapshot();
  void drawHints() const;

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;

  struct FootnoteLink {
    uint16_t link;
    uint16_t footnote;
  };
  std::array<FootnoteLink, Page::MAX_FOOTNOTES_PER_PAGE> footnoteLinks{};
  size_t footnoteLinkCount = 0;
  // Footer records exceed the per-local stack budget; reuse activity-owned scratch.
  FootnoteEntry footnoteScratch;
  PageLink linkScratch;
  XpUiReaderFootnoteState state{};
  int renderingSelected = 0;
  XpUiCommand highlightCommands[2]{};

  // Differential highlight repaint: pixels under the current highlight box.
  static constexpr size_t SNAPSHOT_CAPACITY = 4096;
  std::unique_ptr<uint8_t[]> snapshot;
  int16_t snapshotX = 0;
  int16_t snapshotY = 0;
  int16_t snapshotW = 0;
  int16_t snapshotH = 0;
  int snapshotIdx = -1;
};
