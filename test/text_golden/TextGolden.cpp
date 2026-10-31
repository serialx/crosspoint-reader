#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <TtfEpdFont.h>
#include <Utf8.h>
#include <builtinFonts/all.h>
#include <fontIds.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>

#include "Trace.h"

static std::ostream* trace = nullptr;
static const GfxRenderer* activeRenderer = nullptr;
void textGoldenGlyph(const EpdFontFamily& family, uint32_t cp, int x, int y, EpdFontFamily::Style style) {
  if (!trace) return;
  int id = 0;
  for (const auto& entry : activeRenderer->getFontMap()) {
    if (&entry.second == &family) {
      id = entry.first;
      break;
    }
  }
  *trace << cp << ',' << x << ',' << y << ',' << id << ',' << int(style) << ';';
}
void textGoldenScaled(int fontId, uint32_t cp, int x, int y, EpdFontFamily::Style style) {
  if (trace) *trace << cp << ',' << x << ',' << y << ',' << fontId << ',' << int(style) << ';';
}
static std::string unhex(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); i += 2) out += char(std::stoi(text.substr(i, 2), nullptr, 16));
  return out;
}
static uint64_t hashBytes(const uint8_t* bytes, size_t len) {
  uint64_t h = 14695981039346656037ULL;
  for (size_t i = 0; i < len; ++i) {
    h ^= bytes[i];
    h *= 1099511628211ULL;
  }
  return h;
}
enum class DrawPath { Text, Rotated, Scaled, Words, Centered };
struct DrawWord {
  int x, y, style, direction;
  std::string text;
};
static bool drawCase(std::istream& in, GfxRenderer& renderer, FontCacheManager& cache, HalDisplay& display,
                     const std::string& key, int id, int st, int tracking, int dir, int fallback,
                     const std::string& text) {
  int pathValue, orientation, black, absolute, x, y, cx, cy, cw, ch, stripY, stripRows, scale, wordCount;
  if (!(in >> pathValue >> orientation >> black >> absolute >> x >> y >> cx >> cy >> cw >> ch >> stripY >> stripRows >>
        scale >> wordCount))
    return false;
  if (pathValue < 0 || pathValue > 4 || orientation < 0 || orientation > 3 || wordCount < 0) return false;
  const auto path = static_cast<DrawPath>(pathValue);
  std::vector<DrawWord> words;
  for (int i = 0; i < wordCount; ++i) {
    DrawWord word;
    std::string hex;
    if (!(in >> word.x >> word.y >> word.style >> word.direction >> hex)) return false;
    word.text = unhex(hex);
    words.push_back(std::move(word));
  }
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
  renderer.setClipRect(cx, cy, cw, ch);
  renderer.clearFallbackFonts();
  if (fallback) renderer.setFallbackFont(id, fallback);
  const auto prepare = [&](const std::string& value) {
    const char* segment = value.c_str();
    const size_t length = value.size() + 1;
    renderer.ensureSdCardFontReady(id, &segment, &length, 1, true, true, 15, true);
    if (fallback) renderer.ensureSdCardFontReady(fallback, &segment, &length, 1, true, true, 15, true);
  };
  prepare(text);
  for (const auto& word : words) prepare(word.text);
  const auto style = static_cast<EpdFontFamily::Style>(st);
  const auto base = static_cast<BidiUtils::BidiBaseDir>(dir);
  std::ostringstream advances;
  const auto draw = [&]() {
    switch (path) {
      case DrawPath::Text:
        renderer.drawText(id, x, y, text.c_str(), black, style, base, tracking);
        break;
      case DrawPath::Rotated:
        renderer.drawTextRotated90CW(id, x, y, text.c_str(), black, style);
        break;
      case DrawPath::Scaled: {
        int pen = x;
        const auto* cursor = reinterpret_cast<const uint8_t*>(text.c_str());
        while (const auto cp = utf8NextCodepoint(&cursor)) {
          const int advance = renderer.drawScaledCodepoint(id, cp, style, pen, y, scale);
          if (trace) advances << cp << ',' << advance << ';';
          pen += advance;
        }
        break;
      }
      case DrawPath::Words:
        for (const auto& word : words)
          renderer.drawText(id, x + word.x, y + word.y, word.text.c_str(), black,
                            static_cast<EpdFontFamily::Style>(word.style),
                            static_cast<BidiUtils::BidiBaseDir>(word.direction), tracking);
        break;
      case DrawPath::Centered:
        renderer.drawCenteredText(id, y, text.c_str(), black, style, base);
        break;
    }
  };
  auto scope = cache.createPrewarmScope();
  // Rotated drawing has no scan branch. Scan horizontally to warm the same font tables first.
  if (path == DrawPath::Rotated)
    renderer.drawText(id, x, y, text.c_str(), black, style, base);
  else
    draw();
  scope.endScanAndPrewarm();
  renderer.displayGrayscaleBase(HalDisplay::FULL_REFRESH);
  // Guard bytes expose any write outside a strip, and the untouched framebuffer is hashed too.
  std::vector<uint8_t> strip(static_cast<size_t>(stripRows) * HalDisplay::DISPLAY_WIDTH_BYTES + 32, 0xA5);
  std::cout << key << '\t';
  for (int plane = 0; plane < 3; ++plane) {
    // The reader starts the absolute pass after BW; selecting BW cancels it.
    if (plane == 1 && absolute &&
        !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, HalDisplay::FULL_REFRESH))
      return false;
    renderer.setRenderMode(static_cast<GfxRenderer::RenderMode>(plane));
    if (renderer.grayPlanesAreAbsolute() != (absolute && plane != 0)) return false;
    display.pixels.fill(0x5A);
    if (stripRows) renderer.beginStripTarget(strip.data() + 16, stripY, stripRows);
    renderer.clearScreen(plane == 0 || absolute ? (black ? 0xFF : 0x00) : 0x00);
    trace = plane == 0 ? &std::cout : nullptr;
    draw();
    trace = nullptr;
    renderer.endStripTarget();
    std::cout << '|' << hashBytes(display.pixels.data(), display.pixels.size());
    if (stripRows) std::cout << ',' << hashBytes(strip.data(), strip.size());
  }
  std::cout << '\t' << advances.str() << '\n';
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.displayGrayscaleBase(HalDisplay::FULL_REFRESH);
  renderer.setOrientation(GfxRenderer::Portrait);
  renderer.setClipRect(0, 0, 32767, 32767);
  return true;
}
static void builtinFonts(GfxRenderer& renderer) {
#define READER(SIZE, ID)                                                                \
  static EpdFont r##SIZE(&notosans_##SIZE##_regular), b##SIZE(&notosans_##SIZE##_bold), \
      i##SIZE(&notosans_##SIZE##_italic), bi##SIZE(&notosans_##SIZE##_bolditalic);      \
  renderer.insertFont(ID, EpdFontFamily(&r##SIZE, &b##SIZE, &i##SIZE, &bi##SIZE));
  READER(12, NOTOSANS_12_FONT_ID)
  READER(14, NOTOSANS_14_FONT_ID)
  READER(16, NOTOSANS_16_FONT_ID)
  READER(18, NOTOSANS_18_FONT_ID)
#undef READER
  static EpdFont u8(&ubuntu_8_regular), u10(&ubuntu_10_regular), b10(&ubuntu_10_bold), u12(&ubuntu_12_regular),
      ub12(&ubuntu_12_bold);
  renderer.insertFont(SMALL_FONT_ID, EpdFontFamily(&u8));
  renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&u10, &b10));
  renderer.insertFont(UI_12_FONT_ID, EpdFontFamily(&u12, &ub12));
}
int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "TextGolden font-manifest cases.tsv\n";
    return 2;
  }
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  builtinFonts(renderer);
  FontDecompressor decompressor;
  if (!decompressor.init()) return 3;
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  activeRenderer = &renderer;
  std::vector<std::unique_ptr<SdCardFont>> sdFonts;
  std::vector<std::vector<uint8_t>> fontBytes;
  std::vector<std::unique_ptr<TtfEpdFont>> ttfFonts;
  std::ifstream manifest(argv[1]);
  if (!manifest) return 4;
  std::string line;
  while (std::getline(manifest, line)) {
    std::istringstream in(line);
    char kind;
    int id;
    std::string path;
    in >> kind >> id >> path;
    path = unhex(path);
    if (kind == 'S') {
      auto font = std::make_unique<SdCardFont>();
      if (!font->load(path.c_str())) {
        std::cerr << "Cannot load " << path << '\n';
        return 5;
      }
      renderer.insertFont(
          id, EpdFontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3)));
      renderer.registerSdCardFont(id, font.get());
      sdFonts.push_back(std::move(font));
    } else if (kind == 'T') {
      std::ifstream file(path, std::ios::binary);
      if (!file) return 6;
      fontBytes.emplace_back(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
      auto font = std::make_unique<TtfEpdFont>();
      font->addResidentSource(0, fontBytes.back().data(), fontBytes.back().size());
      if (!font->load(14)) return 7;
      renderer.insertFont(id, font->family());
      renderer.registerTtfFont(id, font.get());
      ttfFonts.push_back(std::move(font));
    } else {
      return 8;
    }
  }
  std::ifstream cases(argv[2]);
  if (!cases) return 9;
  while (std::getline(cases, line)) {
    std::istringstream in(line);
    std::string key, hex;
    int id, st, tracking, dir, mode, draw, fallback;
    uint32_t left, right;
    if (!(in >> key >> id >> st >> tracking >> dir >> mode >> draw >> fallback >> left >> right >> hex)) return 10;
    const auto text = hex == "-" ? std::string() : unhex(hex);
    if (key.starts_with("draw/")) {
      if (!drawCase(in, renderer, cache, display, key, id, st, tracking, dir, fallback, text)) return 12;
      continue;
    }
    const auto style = static_cast<EpdFontFamily::Style>(st);
    const auto base = static_cast<BidiUtils::BidiBaseDir>(dir);
    renderer.clearFallbackFonts();
    if (fallback) renderer.setFallbackFont(id, fallback);
    // Match prepareText's packed paragraph API, including uppercase small caps.
    const char* segment = text.c_str();
    const size_t length = text.size() + 1;
    renderer.ensureSdCardFontReady(id, &segment, &length, 1, true, true, 15, true);
    if (fallback) renderer.ensureSdCardFontReady(fallback, &segment, &length, 1, true, true, 15, true);
    std::optional<FontCacheManager::PrewarmScope> scope;
    if (mode || draw) {
      scope.emplace(cache);
      renderer.drawText(id, 12, 50, text.c_str(), true, style, base, tracking);
      scope->endScanAndPrewarm();
    }
    const int resolved = renderer.resolveTextFontId(id, text.c_str(), style);
    std::cout << key << '\t' << resolved << ','
              << renderer.getTextAdvanceX(id, text.c_str(), style, tracking, base,
                                          static_cast<GfxRenderer::TextMeasureMode>(mode))
              << ',' << renderer.getTextWidth(id, text.c_str(), style, base) << ',' << renderer.getSpaceWidth(id, style)
              << ',' << renderer.getSpaceAdvance(id, left, right, style) << ','
              << renderer.getKerning(id, left, right, style, tracking) << ',' << renderer.getFontAscenderSize(id) << ','
              << renderer.getLineHeight(id) << ',' << renderer.getTextHeight(id) << ','
              << renderer.getLineHeight(id, 0.85f) << '\t';
    const auto* cursor = reinterpret_cast<const uint8_t*>(text.c_str());
    while (const auto cp = utf8NextCodepoint(&cursor)) {
      int32_t advance = 0;
      int top = 0;
      const bool found = renderer.getCodepointMetrics(id, cp, style, advance, top);
      std::cout << cp << ',' << found << ',' << advance << ',' << top << ';';
    }
    std::cout << '\t';
    if (draw) {
      for (int plane = 0; plane < 3; ++plane) {
        renderer.setRenderMode(static_cast<GfxRenderer::RenderMode>(plane));
        renderer.clearScreen(plane == 0 ? 0xFF : 0x00);
        trace = plane == 0 ? &std::cout : nullptr;
        renderer.drawText(id, 12, 50, text.c_str(), true, style, base, tracking);
        trace = nullptr;
        std::cout << '|' << hashBytes(display.pixels.data(), display.pixels.size());
      }
      renderer.setRenderMode(GfxRenderer::BW);
    }
    std::cout << '\n';
  }
  return std::cout.good() ? 0 : 11;
}
