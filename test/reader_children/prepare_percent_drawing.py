"""Compose the current Percent drawing methods with the real renderer and SDK."""
from pathlib import Path
import sys
import re

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "reader_lifecycle"))
from extract import definition

ROOT = Path(__file__).resolve().parents[2]


def read(name):
    return (ROOT / name).read_text()


slider = read("src/components/UiSliderDialog.h")
helpers = read("src/components/UiAppHelpers.h")
utility = read("src/activities/util/UtilityActivity.cpp")
sample = utility[utility.index("  const auto& m = UITheme::getInstance().getMetrics();"):]
sample = sample[:sample.index("  utilityLayout(l);")] + "  return l;\n"
definitions = [
    definition(read("src/components/UIThemeTokens.h"), "inline freeink::ui::ThemeTokens uiThemeTokens("),
    definition(helpers, "inline std::atomic<const freeink::ui::ThemeTokens*>& sharedUiThemeCell("),
    definition(helpers, "inline const freeink::ui::ThemeTokens& refreshSharedUiThemeTokens("),
    definition(helpers, "inline freeink::ui::GfxRendererTarget makeUiTarget("),
    definition(slider, "struct UiSliderDialogSpec {") + ";",
    definition(slider, "inline void buildSliderDialogScreen("),
    definition(read("src/activities/reader/EpubReaderPercentSelectionActivity.cpp"),
               "void EpubReaderPercentSelectionActivity::buildPercentScreen("),
    *[definition(read("src/activities/reader/EpubReaderPercentSelectionActivity.cpp"), signature) for signature in (
        "void EpubReaderPercentSelectionActivity::setPercent(",
        "void EpubReaderPercentSelectionActivity::onSliderEvent(",
    )],
    "XpUiUtilityLayout sampleLayout(GfxRenderer& renderer, const MappedInputManager& mappedInput) {\n"
    + sample + "}",
]
base = read("src/components/themes/BaseTheme.cpp")
for signature in ("void BaseTheme::drawHintLabel(", "int BaseTheme::headerStatusInset(",
                  "void BaseTheme::applyHeaderStatus(", "freeink::ui::Rect BaseTheme::headerBand("):
    definitions.append(definition(base, signature).replace("BaseTheme::", ""))
for path, owner, method, renamed in (
    ("src/components/themes/BaseTheme.cpp", "BaseTheme", "drawHeader", "nativeHeader"),
    ("src/components/themes/roundedraff/RoundedRaffTheme.cpp", "RoundedRaffTheme", "drawHeader", "nativeRoundedHeader"),
    ("src/components/themes/BaseTheme.cpp", "BaseTheme", "drawButtonHints", "nativeBaseHints"),
    ("src/components/themes/lyra/LyraTheme.cpp", "LyraTheme", "drawButtonHints", "nativeLyraHints"),
    ("src/components/themes/roundedraff/RoundedRaffTheme.cpp", "RoundedRaffTheme", "drawButtonHints", "nativeRoundedHints"),
):
    body = definition(read(path), "void " + owner + "::" + method + "(")
    body = body.replace(owner + "::" + method, renamed).replace("BaseTheme::drawHeader", "nativeHeader")
    definitions.append(re.sub(r"\)\s+const\s*\{", ") {", body, count=1))
definitions.append("""
void Gui::drawHeader(GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle, bool enabled) {
  header = rect; back = enabled;
  if (nativeTheme == 1) nativeRoundedHeader(renderer, rect, title, subtitle, enabled);
  else nativeHeader(renderer, rect, title, subtitle, enabled);
}
void Gui::drawButtonHints(GfxRenderer& renderer, const char* a, const char* b, const char* c, const char* d) {
  footerClip = renderer.getClipRect();
  switch (nativeTheme) {
    case 0: nativeBaseHints(renderer, a,b,c,d); break;
    case 1: nativeRoundedHints(renderer, a,b,c,d); break;
    case 2: nativeLyraHints(renderer, a,b,c,d); break;
    default: std::abort();
  }
}
""")
physical = read("src/activities/reader/RustReaderDrawing.cpp")
definitions.append("""
struct UtilityActivity {
  enum { Footer = 1001 };
  GfxRenderer& renderer;
  bool utilityResourceValid(uint32_t handle) { return handle == Footer; }
  void utilityResource(const XpUiCommand& command) {
    assert(command.resource == Footer);
    GUI.drawButtonHints(renderer, "four", "three", "two", "one");
  }
  XpUiPaint rustResources();
};
""")
definitions.append(definition(utility, "XpUiPaint UtilityActivity::rustResources("))
definitions.extend([
    definition(read("src/activities/reader/ReaderChromeListActivity.cpp"), "void sampleReaderChromeTheme("),
    definition(physical, "const char* text("),
    *[definition(physical, signature) for signature in (
        "bool sampleReaderUtilityLayout(", "void sampleReaderChildTheme(",
        "void drawReaderHeader(", "void drawReaderUtilityFooter(",
        "void drawReaderToolbarIcon(", "void prewarmReaderRows(",
    )],
])
template = Path(__file__).with_name("percent_drawing.cpp.in").read_text()
Path(sys.argv[1]).write_text(template.replace("@DRAWING@", "\n".join(definitions)))
