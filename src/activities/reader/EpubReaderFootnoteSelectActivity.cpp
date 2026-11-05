#include "EpubReaderFootnoteSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

void EpubReaderFootnoteSelectActivity::onEnter() {
  Activity::onEnter();
  snapshot = makeUniqueNoThrow<uint8_t[]>(SNAPSHOT_CAPACITY);
  {
    RenderLock lock;
    buildFootnoteLinks();
  }
  requestUpdate();
}

void EpubReaderFootnoteSelectActivity::buildFootnoteLinks() {
  footnoteLinkCount = 0;
  // Preserve footnote order and the first link with the same href.
  for (uint16_t i = 0; i < page->footnoteCount(); ++i) {
    page->footnote(i, footnoteScratch);
    for (uint16_t j = 0; j < page->linkCount(); ++j) {
      page->link(j, linkScratch);
      if (strcmp(linkScratch.href, footnoteScratch.href) == 0) {
        footnoteLinks[footnoteLinkCount++] = {j, i};
        break;
      }
    }
  }
}

void EpubReaderFootnoteSelectActivity::performJump(int index) {
  if (footnoteLinkCount == 0) return;
  ActivityResult result;
  page->footnote(footnoteLinks[index].footnote, footnoteScratch);
  result.data = FootnoteResult{footnoteScratch.href};
  setResult(std::move(result));
  finish();
}

void EpubReaderFootnoteSelectActivity::drawHints() const {
  const auto labels = mappedInput.mapDirectionalLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT),
                                                       tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

XpUiEffect EpubReaderFootnoteSelectActivity::updateRust(const XpUiInput& input) {
  return xp_ui_reader_footnote_update(&state, &input, footnoteLinkCount);
}

void EpubReaderFootnoteSelectActivity::applyRustEffect(const XpUiEffect& effect) {
  switch (static_cast<XpUiEffectKind>(effect.kind)) {
    case XP_UI_CANCEL: {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      break;
    }
    case XP_UI_ACTIVATE: {
      RenderLock lock;
      if (rustSceneRejected()) {
        requestUpdate();
        return;
      }
      performJump(effect.value);
      break;
    }
    case XP_UI_EFFECT_NONE:
    case XP_UI_SCROLL:
      break;
  }
}

bool EpubReaderFootnoteSelectActivity::drawHighlightWithSnapshot() {
  if (footnoteLinkCount == 0) return false;
  page->link(footnoteLinks[renderingSelected].link, linkScratch);
  page->footnote(footnoteLinks[renderingSelected].footnote, footnoteScratch);
  const XpUiRect screen{0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                        static_cast<int16_t>(renderer.getScreenHeight())};
  const XpUiRect link{static_cast<int16_t>(linkScratch.x), static_cast<int16_t>(linkScratch.y),
                      static_cast<int16_t>(linkScratch.width), static_cast<int16_t>(linkScratch.height)};
  const XpUiBuffer buffer{highlightCommands, nullptr, 2, 0};
  XpUiRect rect{};
  XpUiScene scene{};
  if (!xp_ui_reader_footnote_view(screen, link, marginLeft, marginTop, SETTINGS.getReaderFontId(),
                                  footnoteScratch.number[0] ? 1 : 0, &buffer, &rect, &scene)) {
    LOG_ERR("RUI", "Cannot build footnote highlight");
    snapshotIdx = -1;
    rejectRustScene();
    return false;
  }
  const bool saved =
      snapshot && rect.w > 0 && rect.h > 0 &&
      renderer.readFramebufferRegion(rect.x, rect.y, rect.w, rect.h, snapshot.get(), SNAPSHOT_CAPACITY) > 0;
  snapshotX = rect.x;
  snapshotY = rect.y;
  snapshotW = rect.w;
  snapshotH = rect.h;
  snapshotIdx = saved ? renderingSelected : -1;
  const XpUiPaint resources{
      this, nullptr, nullptr,
      [](void*, uint32_t handle, uint32_t generation) -> uint8_t { return handle == 1 && generation == 1; },
      [](void* p, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
        auto& self = *static_cast<EpubReaderFootnoteSelectActivity*>(p);
        self.renderer.drawUiText(*command, clip, self.footnoteScratch.number);
      }};
  if (!renderer.drawUiScene(scene, &resources)) {
    LOG_ERR("RUI", "Invalid footnote highlight");
    snapshotIdx = -1;
    rejectRustScene();
    return false;
  }
  publishRustScene(scene);
  return saved;
}

void EpubReaderFootnoteSelectActivity::render(RenderLock&&) {
  {
    ModelLock lock(*this);
    renderingSelected = state.selected;
  }
  // Differential fast path: only the highlight moved and the framebuffer still
  // holds a clean page.
  if (snapshotIdx >= 0 && footnoteLinkCount != 0 && renderingSelected != snapshotIdx) {
    renderer.writeFramebufferRegion(snapshotX, snapshotY, snapshotW, snapshotH, snapshot.get());
    if (drawHighlightWithSnapshot()) {
      drawHints();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      return;
    }
    // Snapshot failed — fall through to a full repaint.
  }

  renderer.clearScreen();

  // Render the page underneath
  if (page) {
    page->render(renderer, SETTINGS.getReaderFontId(), marginLeft, marginTop);
  }

  if (footnoteLinkCount != 0) {
    drawHighlightWithSnapshot();
    if (rustSceneRejected()) return;
  } else {
    publishRustScene(XpUiScene{});
  }

  drawHints();

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
