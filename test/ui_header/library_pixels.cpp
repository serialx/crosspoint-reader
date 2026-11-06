#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkUIGfxRenderer.h>
#include <FreeInkUIIcon.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_ui_reader_chrome.h>

#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>

#include "Trace.h"
#include "components/UiHeader.h"
#include "components/icons/headerIcons.h"
#include "components/icons/listIcons.h"
#include "components/icons/search32.h"

void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
namespace fui = freeink::ui;

std::atomic<const fui::ThemeTokens*>& sharedUiThemeCell() {
  static std::atomic<const fui::ThemeTokens*> cell;
  return cell;
}

// Deterministic status and bezel leaves; both painters use the same SDK and renderer.
struct BaseTheme {
  static fui::Rect headerBand(const GfxRenderer&, fui::Rect rect, fui::HeaderProps&) { return rect; }
};
struct Status {
  bool enabled = false;
  void applyHeaderStatus(const GfxRenderer&, fui::HeaderProps& props) const {
    if (!enabled) return;
    props.status.showBattery = true;
    props.status.battery.label = "57%";
    props.status.stripHeight = 20;
    props.status.clockText = "12:34";
    props.titleOffsetY = 16;
    props.actionOffsetY = 16;
    props.leadingSize = props.trailingSize = 32;
  }
} GUI;

struct HeaderOwner {
  GfxRenderer& renderer;
  fui::GfxRendererTarget textTarget;
  const char* title = "Library";
  struct {
    XpUiReaderControl headerControls[3]{};
    uint8_t headerCount = 0;
  } frame;
  struct {
    fui::HeaderProps props;
    fui::InteractionBuffer<24> controls;
  } headerScratch;
  explicit HeaderOwner(GfxRenderer& renderer) : renderer(renderer), textTarget(renderer) {
    for (int i = 0; i < 4; ++i) textTarget.setFont(i, UI_10_FONT_ID);
  }
  const char* headerTitle() const { return title; }
  void paintHeader(const XpUiCommand&);
  void paintBaseline(const XpUiCommand&);
};

#include "library_methods.inc"

int main(int argc, char** argv) {
  assert(argc == 2);
  size_t cases = 0;
  for (bool fault : {false, true}) {
    // Separate fonts/caches make the parent's draw unable to warm the candidate.
    HalDisplay displays[2];
    GfxRenderer renderers[]{GfxRenderer(displays[0]), GfxRenderer(displays[1])};
    SdCardFont fonts[2];
    FontDecompressor decompressors[2];
    std::unique_ptr<FontCacheManager> caches[2];
    for (int side = 0; side < 2; ++side) {
      renderers[side].begin();
      assert(fonts[side].load(argv[1]));
      renderers[side].insertFont(UI_10_FONT_ID, EpdFontFamily(fonts[side].getEpdFont(0), fonts[side].getEpdFont(1)));
      renderers[side].registerSdCardFont(UI_10_FONT_ID, &fonts[side]);
      assert(decompressors[side].init());
      caches[side] = std::make_unique<FontCacheManager>(renderers[side].getFontMap(), renderers[side].getSdCardFonts(),
                                                        renderers[side].getTtfFonts());
      caches[side]->setFontDecompressor(&decompressors[side]);
      renderers[side].setFontCacheManager(caches[side].get());
    }
    HeaderOwner baseline(renderers[0]), candidate(renderers[1]);
    auto theme = fui::themeTokensForLineHeight(24);
    sharedUiThemeCell().store(&theme);
    headerFailReads = fault;
    headerFailedReads = 0;
    size_t errors = 0;
    for (int orientation = 0; orientation < 4; ++orientation) {
      for (auto& renderer : renderers) renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
      for (const char* title :
           {"Library", "A very long Library search title repeated until it must truncate at the edge",
            "도서관 검색 결과 漢字 日本語 긴 제목으로 잘림 확인"}) {
        baseline.title = candidate.title = title;
        // Descending masks exercise removal of every previously painted action.
        for (int mask = 7; mask >= 0; --mask) {
          GUI.enabled = mask & 1;
          theme.headerTitleAlign = mask & 2 ? fui::TextAlign::Center : fui::TextAlign::Left;
          XpUiCommand command{};
          command.rect = {0, 0, 200, 80};
          command.style = mask;
          renderers[0].clearScreen();
          renderers[1].clearScreen();
          headerLogs.clear();
          baseline.paintBaseline(command);
          const auto logs = headerLogs;
          errors +=
              std::count_if(logs.begin(), logs.end(), [](const auto& line) { return line.starts_with("E:SDCF:"); });
          headerLogs.clear();
          candidate.paintHeader(command);
          assert(logs == headerLogs);
          assert(displays[0].pixels == displays[1].pixels);
          assert(baseline.frame.headerCount == candidate.frame.headerCount);
          for (int i = 0; i < baseline.frame.headerCount; ++i) {
            const auto& a = baseline.frame.headerControls[i];
            const auto& b = candidate.frame.headerControls[i];
            assert(a.rect.x == b.rect.x && a.rect.y == b.rect.y && a.rect.w == b.rect.w && a.rect.h == b.rect.h);
            assert(a.action == b.action && a.value == b.value);
          }
          ++cases;
        }
      }
    }
    if (fault) assert(headerFailedReads > 0 && errors > 0);
    headerFailReads = false;
  }
  std::cout << cases << " Library header compositions: equal pixels, logs, and controls; SD short reads exercised\n";
}
