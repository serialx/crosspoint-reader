"""Compile unchanged baseline measurement bodies without display hardware."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[2]
source = subprocess.check_output(
    ["git", "show", "c9330408:lib/GfxRenderer/GfxRenderer.cpp"], cwd=root, text=True
)


def function(signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


names = [
    "constexpr int32_t scaleSmallCapAdvance(", "constexpr int trackingBetween(",
    "uint8_t resolveSdCardStyle(", "uint16_t getSdCardSpaceAdvance(",
    "int GfxRenderer::resolveTextFontId(", "int GfxRenderer::getTextWidth(",
    "int GfxRenderer::getSpaceWidth(", "int GfxRenderer::getSpaceAdvance(",
    "int GfxRenderer::getKerning(", "int GfxRenderer::getTextAdvanceX(",
    "bool GfxRenderer::getCodepointMetrics(", "int GfxRenderer::getFontAscenderSize(",
    "int GfxRenderer::getLineHeight(const int fontId)",
    "int GfxRenderer::getLineHeight(const int fontId, const float",
    "int GfxRenderer::getTextHeight(",
]
prefix = "constexpr int SMALL_CAPS_SCALE_NUM = 3;\nconstexpr int SMALL_CAPS_SCALE_DEN = 4;\n"
pathlib.Path(sys.argv[1]).write_text(prefix + "\n".join(map(function, names)))
# Preparation uses the baseline byte scan and bidi implementation, exactly once.
visual = function("const char* resolveVisualText(const char* text, std::string& visualBuffer, const")
pathlib.Path(sys.argv[2]).write_text(visual.replace("resolveVisualText", "prepareVisualText", 1))
