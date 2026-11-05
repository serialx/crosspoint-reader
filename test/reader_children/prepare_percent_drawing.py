"""Compose the current Percent drawing methods with the real renderer and SDK."""
from pathlib import Path
import sys

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
    "XpUiUtilityLayout sampleLayout(GfxRenderer& renderer, const MappedInputManager& mappedInput) {\n"
    + sample + "}",
]
physical = read("src/activities/reader/RustReaderDrawing.cpp")
definitions.extend([
    definition(read("src/activities/reader/ReaderChromeListActivity.cpp"), "void sampleReaderChromeTheme("),
    definition(physical, "const char* text("),
    *[definition(physical, signature) for signature in (
        "void sampleReaderUtilityLayout(", "void sampleReaderChildTheme(",
        "void drawReaderHeader(", "void drawReaderUtilityFooter(",
        "void drawReaderToolbarIcon(", "void prewarmReaderRows(",
    )],
])
template = Path(__file__).with_name("percent_drawing.cpp.in").read_text()
Path(sys.argv[1]).write_text(template.replace("@DRAWING@", "\n".join(definitions)))
