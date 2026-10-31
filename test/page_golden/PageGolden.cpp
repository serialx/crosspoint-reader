#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <Epub/SectionCodec.h>
#include <Epub/css/CssParser.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <ReaderFontSizes.h>
#include <SdCardFont.h>
#include <builtinFonts/all.h>
#include <clippings/ClippingText.h>
#include <fontIds.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "PageTrace.h"
#ifdef XP_STRIP_SPIKE
#include "StripTrace.h"
std::vector<StripBounds> stripBounds;
StripStats stripStats{};
int stripPhase = 0;
#endif

extern "C" size_t xp_test_allocation_attempts();
size_t pageGoldenImageAllocations = 0;
static uint64_t owners[12]{};
void pageGoldenOwner(unsigned kind) { ++owners[kind]; }
static uint64_t nativeCalls = 0, emissions = 0, scaledCalls = 0;
void pageGoldenNativeDraw() { ++nativeCalls; }
void pageGoldenEmission() { ++emissions; }
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) { ++scaledCalls; }

static uint64_t hashBytes(const uint8_t* bytes, size_t length) {
  uint64_t h = 14695981039346656037ULL;
  for (size_t i = 0; i < length; ++i) h = (h ^ bytes[i]) * 1099511628211ULL;
  return h;
}
static std::string hex(std::string_view value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (unsigned char c : value) {
    result += digits[c >> 4];
    result += digits[c & 15];
  }
  return result;
}
static std::string readBytes(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
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
bool readManifest(Epub& epub, std::vector<std::string>& cssPaths) {
  std::ifstream manifest("manifest.tsv");
  if (!manifest) return false;
  std::string line;
  while (std::getline(manifest, line)) {
    const auto tab = line.find('\t');
    if (tab == std::string::npos) return false;
    const auto value = line.substr(tab + 1);
    if (line.starts_with("spine\t"))
      epub.spine.push_back({value});
    else if (line.starts_with("css\t"))
      cssPaths.push_back(value);
    else if (line.starts_with("language\t"))
      epub.language = value;
    else if (line.starts_with("toc\t")) {
      const auto split = value.find('\t');
      if (split == std::string::npos) return false;
      epub.toc.push_back({std::stoi(value.substr(0, split)), value.substr(split + 1)});
    } else
      return false;
  }
  return !epub.spine.empty();
}

struct Setting {
  ReaderRenderSpec spec;
  int margin, orientation;
};
static Setting setting(int config, int sdFont) {
  Setting result{};
  auto& spec = result.spec;
  const int fonts[] = {NOTOSANS_12_FONT_ID, NOTOSANS_18_FONT_ID, UI_12_FONT_ID, sdFont};
  spec.fontId = fonts[config];
  result.orientation = config;
  result.margin = config == 1 ? 32 : 12;
  const bool landscape = config % 2;
  spec.viewportWidth = (landscape ? 800 : 480) - 2 * result.margin;
  spec.viewportHeight = (landscape ? 480 : 800) - 2 * result.margin;
  spec.hyphenationEnabled = config != 0;
  spec.focusReadingEnabled = config == 1;
  spec.extraParagraphSpacing = config == 1;
  spec.paragraphIndentSpaces = config == 2 ? 0 : 2;
  spec.lineCompression = config == 2 ? 1.25f : config == 3 ? 0.85f : 1.0f;
  spec.characterSpacing = config == 2 ? 1 : 0;
  spec.wordSpacingPercent = config == 2 ? 125 : 100;
  spec.paragraphAlignment = config;  // justify, left, center, right
  spec.embeddedStyle = true;
  spec.imageRendering = 0;
  return result;
}

// Definition lookup uses the uncapped word list; the reader caps this same geometry at 240 words.
static void queries(const Page& page, GfxRenderer& renderer, const Setting& config) {
  struct Word {
    int x, y, width, height, row, index, font;
    uint32_t start, end;
    bool paragraph, hyphen;
    std::string text;
  };
  std::vector<Word> words;
  int row = 0, ordinal = 0;
  bool paragraphPending = false;
  for (uint16_t index = 0; index < page.elementCount(); ++index) {
    const auto element = page.element(index);
    if (element.getTag() != TAG_PageLine) continue;
    const auto& line = element;
    const auto block = line.textBlock();
    if (!block.valid()) continue;
    const auto start = words.size();
    const int lineFont = TextBlock::renderFontId(renderer, block.blockFontId(config.spec.fontId), config.spec.fontId);
    const int rubyShift = block.getRubyShift(renderer.getFontAscenderSize(lineFont));
    for (uint16_t i = 0; i < block.wordCount(); ++i) {
      if (block.wordStartsParagraph(i)) paragraphPending = true;
      const char* text = block.wordText(i);
      if (!clippingText::hasVisibleText(text)) continue;
      const auto style = static_cast<EpdFontFamily::Style>(block.wordStyle(i) & ~EpdFontFamily::UNDERLINE);
      const int font = TextBlock::renderFontId(renderer, block.wordFontId(i, config.spec.fontId), config.spec.fontId);
      int width = renderer.getTextAdvanceX(font, text, style, block.getBlockStyle().characterSpacing);
      if (width <= 0) continue;
      if (i + 1 < block.wordCount() && block.wordXpos(i + 1) > block.wordXpos(i))
        width = std::min(width, int(block.wordXpos(i + 1) - block.wordXpos(i)));
      const auto range = block.wordSourceRange(i);
      words.push_back({config.margin + line.xPos + block.wordXpos(i),
                       config.margin + line.yPos + rubyShift + block.wordYOffset(renderer, config.spec.fontId, i),
                       width, renderer.getLineHeight(font), row, ordinal++, font, range.start, range.end, false,
                       block.wordHasDiscretionaryHyphen(i), text});
    }
    if (block.getBlockStyle().isRtl) std::reverse(words.begin() + start, words.end());
    if (words.size() > start) {
      if (paragraphPending) {
        std::min_element(words.begin() + start, words.end(), [](const Word& a, const Word& b) {
          return a.start < b.start;
        })->paragraph = true;
        paragraphPending = false;
      }
      ++row;
    }
  }
  const auto hit = [&](int x, int y) {
    for (size_t i = 0; i < words.size(); ++i) {
      const auto& w = words[i];
      if (x >= w.x - 4 && x < w.x + w.width + 4 && y >= w.y - 4 && y < w.y + w.height + 4) return int(i);
    }
    return -1;
  };
  std::string selection;
  for (size_t i = 0; i < words.size(); ++i) {
    const auto& w = words[i];
    std::cout << "query\t" << i << '\t' << w.x << ',' << w.y << ',' << w.width << ',' << w.height << ',' << w.row << ','
              << w.index << ',' << w.font << ',' << w.start << ',' << w.end << ',' << w.paragraph << ',' << w.hyphen
              << '\t' << hit(w.x + w.width / 2, w.y + w.height / 2) << ',' << hit(w.x - 4, w.y - 4) << ','
              << hit(w.x + w.width + 4, w.y) << '\n';
    clippingText::append(selection, w.text, i && words[i - 1].hyphen ? '\0' : ' ', SIZE_MAX, w.hyphen);
  }
  std::cout << "selection\t" << hex(selection) << '\n';
}

static bool records(Page& page, GfxRenderer& renderer, const Setting& config) {
  const int font = config.spec.fontId;
  int16_t x = 0, y = 0, w = 0, h = 0;
  const bool box = page.getImageBoundingBox(x, y, w, h);
  std::cout << "page\t" << page.visibleTextOffset << '\t' << page.elementCount() << ',' << page.footnoteCount() << ','
            << page.linkCount() << ',' << page.hasImages() << ',' << page.hasImagesNeedingDecode() << ',' << box << ','
            << x << ',' << y << ',' << w << ',' << h << '\n';
  for (uint16_t index = 0; index < page.elementCount(); ++index) {
    const auto element = page.element(index);
    HalFile wire("element.bin");
    if (!element.serialize(wire)) return false;
    wire.close();
    std::cout << "element\t" << int(element.getTag()) << '\t' << element.xPos << ',' << element.yPos << '\t'
              << hex(readBytes("element.bin")) << '\n';
    if (element.getTag() != TAG_PageLine) {
      HalFile fields;
      if (!fields.open("element.bin", "rb")) return false;
      XpSecRecord value;
      XpSecKind kind = XP_SEC_U8;
      size_t count = 0;
      switch (element.getTag()) {
        case TAG_PageImage: {
          std::string path, source;
          if (!sectioncodec::read(fields, XP_SEC_POSITION, value) || !sectioncodec::readString(fields, path) ||
              !sectioncodec::readString(fields, source))
            return false;
          std::cout << "image-paths\t" << hex(path) << '\t' << hex(source) << '\n';
          kind = XP_SEC_IMAGE_SIZE;
          count = 2;
          break;
        }
        case TAG_PageHorizontalRule:
          kind = XP_SEC_RULE;
          count = 4;
          break;
        case TAG_PageDropCap:
          kind = XP_SEC_DROP_CAP;
          count = 6;
          break;
        case TAG_PageBorderBox:
          kind = XP_SEC_BORDER;
          count = 13;
          break;
        default:
          return false;
      }
      if (!sectioncodec::read(fields, kind, value)) return false;
      std::cout << "element-fields\t" << int(element.getTag());
      for (size_t i = 0; i < count; ++i) std::cout << '\t' << value.values[i];
      if (element.getTag() == TAG_PageDropCap) std::cout << '\t' << hex(element.dropCapText());
      std::cout << '\n';
      continue;
    }
    const auto block = element.textBlock();
    const auto& s = block.getBlockStyle();
    std::cout << "line\t" << block.valid() << ',' << block.wordCount() << ',' << block.blockFontId(font) << ','
              << block.lineAscent(renderer, font) << ',' << block.extraAscent(renderer, font) << ',' << block.hasRuby()
              << '\n';
    std::cout << "style\t" << int(s.alignment) << ',' << int(s.characterSpacing) << ',' << s.marginTop << ','
              << s.marginBottom << ',' << s.marginLeft << ',' << s.marginRight << ',' << s.paddingTop << ','
              << s.paddingBottom << ',' << s.paddingLeft << ',' << s.paddingRight << ',' << s.textIndent << ','
              << s.textIndentDefined << ',' << s.textAlignDefined << ',' << s.isRtl << ',' << s.directionDefined << ','
              << s.fontScale << ',' << s.fontScaleDefined << ',' << s.fontId << ',' << s.pageBreakBefore << ','
              << s.pageBreakAfter << ',' << s.keepWithNext << '\n';
    for (uint16_t i = 0; i < block.wordCount(); ++i) {
      const auto range = block.wordSourceRange(i);
      std::cout << "word\t" << i << '\t' << hex(block.wordText(i)) << '\t' << block.wordTextLen(i) << ','
                << block.wordXpos(i) << ',' << int(block.wordStyle(i)) << ',' << block.wordHasSmallCaps(i) << ','
                << block.wordHasDiscretionaryHyphen(i) << ',' << block.wordFontId(i, font) << ','
                << int(block.focusBoundary(i)) << ',' << block.focusSuffixX(i) << ',' << range.start << ',' << range.end
                << ',' << block.wordStartsParagraph(i) << ',' << block.wordYOffset(renderer, font, i) << '\n';
    }
    for (uint16_t i = 0; i < block.rubyCount(); ++i) std::cout << "ruby\t" << hex(block.rubyText(i)) << '\n';
  }
  for (uint16_t i = 0; i < page.footnoteCount(); ++i) {
    FootnoteEntry note;
    page.footnote(i, note);
    std::cout << "footnote\t" << hex(note.number) << '\t' << hex(note.href) << '\n';
  }
  for (uint16_t i = 0; i < page.linkCount(); ++i) {
    PageLink link;
    page.link(i, link);
    std::cout << "link\t" << hex(link.href) << '\t' << link.x << ',' << link.y << ',' << link.width << ','
              << link.height << '\n';
  }
  queries(page, renderer, config);
  return true;
}

static bool pixels(const Page& page, GfxRenderer& renderer, FontCacheManager& cache, HalDisplay& display,
                   const Setting& config, bool sample) {
  for (int orientation = 0; orientation < 4; ++orientation) {
    renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
    for (int placeholders = 0; placeholders <= int(page.hasImages()); ++placeholders) {
      const auto draw = [&] {
        const size_t allocations = xp_test_allocation_attempts() - pageGoldenImageAllocations;
        if (placeholders)
          page.renderWithImagePlaceholders(renderer, config.spec.fontId, config.margin, config.margin);
        else
          page.render(renderer, config.spec.fontId, config.margin, config.margin);
        if (xp_test_allocation_attempts() - pageGoldenImageAllocations != allocations) {
          std::cerr << "Rust allocation during page rendering\n";
          std::exit(1);
        }
      };
      std::fill(std::begin(owners), std::end(owners), 0);
      auto scope = cache.createPrewarmScope();
      draw();
      std::array<uint64_t, 12> scanOwners;
      std::copy(std::begin(owners), std::end(owners), scanOwners.begin());
      scope.endScanAndPrewarm();
      for (int absolute = 0; absolute < 2; ++absolute) {
        for (int band : {0, 31, 80, 127}) {
          renderer.setRenderMode(GfxRenderer::BW);
          renderer.displayGrayscaleBase(HalDisplay::FULL_REFRESH);
          for (int plane = 0; plane < 3; ++plane) {
            if (plane == 1 && absolute &&
                !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, HalDisplay::FULL_REFRESH))
              return false;
            renderer.setRenderMode(static_cast<GfxRenderer::RenderMode>(plane));
            if (renderer.grayPlanesAreAbsolute() != bool(absolute && plane != 0)) return false;
            nativeCalls = emissions = scaledCalls = 0;
            std::fill(std::begin(owners), std::end(owners), 0);
            const uint8_t clear = plane == 0 || absolute ? 0xff : 0;
            std::array<uint8_t, HalDisplay::BUFFER_SIZE> assembled{};
            display.pixels.fill(0x5a);
            if (band == 0) {
              renderer.clearScreen(clear);
              draw();
              assembled = display.pixels;
            } else {
              for (int row = 0; row < HalDisplay::DISPLAY_HEIGHT; row += band) {
                const int rows = std::min(band, int(HalDisplay::DISPLAY_HEIGHT) - row);
                std::vector<uint8_t> strip(rows * HalDisplay::DISPLAY_WIDTH_BYTES + 32, 0xa5);
                renderer.beginStripTarget(strip.data() + 16, row, rows);
                renderer.clearScreen(clear);
                draw();
                renderer.endStripTarget();
                for (int i = 0; i < 16; ++i)
                  if (strip[i] != 0xa5 || strip[strip.size() - 1 - i] != 0xa5) return false;
                std::copy(strip.begin() + 16, strip.end() - 16,
                          assembled.begin() + row * HalDisplay::DISPLAY_WIDTH_BYTES);
              }
              for (uint8_t byte : display.pixels)
                if (byte != 0x5a) return false;
            }
            std::cout << "pixels\t" << orientation << ',' << placeholders << ',' << absolute << ',' << band << ','
                      << plane << '\t' << hashBytes(assembled.data(), assembled.size()) << '\n';
            std::cout << "calls\t" << nativeCalls << ',' << emissions << ',' << scaledCalls;
            for (auto count : owners) std::cout << ',' << count;
            for (auto count : scanOwners) std::cout << ',' << count;
            std::cout << '\n';
            if (sample && orientation == config.orientation && !placeholders && !absolute && !band) {
              std::ofstream pbm("sample-" + std::to_string(plane) + ".pbm", std::ios::binary);
              pbm << "P4\n800 480\n";
              for (uint8_t byte : assembled) pbm.put(char(~byte));
            }
          }
        }
      }
    }
  }
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(config.orientation));
  return true;
}

#ifdef XP_STRIP_SPIKE
static bool stripPixels(const Page& page, GfxRenderer& renderer, FontCacheManager& cache, HalDisplay& display,
                        const Setting& config, bool sample) {
  stripBounds.resize(std::max(size_t(1), page.elementCount()));
  for (int orientation = 0; orientation < 4; ++orientation) {
    renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
    for (int placeholders = 0; placeholders <= int(page.hasImages()); ++placeholders) {
      const auto draw = [&] {
        const size_t allocations = xp_test_allocation_attempts() - pageGoldenImageAllocations;
        if (placeholders)
          page.renderWithImagePlaceholders(renderer, config.spec.fontId, config.margin, config.margin);
        else
          page.render(renderer, config.spec.fontId, config.margin, config.margin);
        if (xp_test_allocation_attempts() - pageGoldenImageAllocations != allocations) {
          std::cerr << "Rust allocation during page rendering\n";
          std::exit(1);
        }
      };
      std::fill(std::begin(owners), std::end(owners), 0);
      auto scope = cache.createPrewarmScope();
      draw();
      std::array<uint64_t, 12> scanOwners;
      std::copy(std::begin(owners), std::end(owners), scanOwners.begin());
      scope.endScanAndPrewarm();
      for (int absolute = 0; absolute < 2; ++absolute) {
        std::array<std::array<uint8_t, HalDisplay::BUFFER_SIZE>, 3> reference{};
        stripPhase = 1;
        stripStats = {};
        std::array<uint8_t, HalDisplay::DISPLAY_WIDTH_BYTES + 32> prepareBuffer;
        prepareBuffer.fill(0xa5);
        display.pixels.fill(0x5a);
        renderer.beginStripTarget(prepareBuffer.data() + 16, 0, 1);
        const auto prepareStart = std::chrono::steady_clock::now();
        draw();
        renderer.endStripTarget();
        const auto prepareUs =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - prepareStart)
                .count();
        for (uint8_t byte : prepareBuffer)
          if (byte != 0xa5) return false;
        for (uint8_t byte : display.pixels)
          if (byte != 0x5a) return false;
        const auto prepareStats = stripStats;
        stripPhase = 0;
        for (int band : {0, 1, 8, 16, 32, 37, 48, 64, 80, 160, int(HalDisplay::DISPLAY_HEIGHT)}) {
          renderer.setRenderMode(GfxRenderer::BW);
          renderer.displayGrayscaleBase(HalDisplay::FULL_REFRESH);
          for (int plane = 0; plane < 3; ++plane) {
            if (plane == 1 && absolute &&
                !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, HalDisplay::FULL_REFRESH))
              return false;
            renderer.setRenderMode(static_cast<GfxRenderer::RenderMode>(plane));
            if (renderer.grayPlanesAreAbsolute() != bool(absolute && plane != 0)) return false;
            nativeCalls = emissions = scaledCalls = 0;
            std::fill(std::begin(owners), std::end(owners), 0);
            const uint8_t clear = plane == 0 || absolute ? 0xff : 0;
            std::array<uint8_t, HalDisplay::BUFFER_SIZE> assembled{};
            display.pixels.fill(0x5a);
            stripStats = {};
            stripPhase = band ? 2 : 0;
            std::vector<uint8_t> strip(band * HalDisplay::DISPLAY_WIDTH_BYTES + 32, 0xa5);
            const auto start = std::chrono::steady_clock::now();
            if (band == 0) {
              renderer.clearScreen(clear);
              draw();
              assembled = display.pixels;
            } else {
              for (int row = 0; row < HalDisplay::DISPLAY_HEIGHT; row += band) {
                const int rows = std::min(band, int(HalDisplay::DISPLAY_HEIGHT) - row);
                std::fill(strip.begin(), strip.end(), 0xa5);
                const auto stripEnd = strip.begin() + 16 + rows * HalDisplay::DISPLAY_WIDTH_BYTES;
                renderer.beginStripTarget(strip.data() + 16, row, rows);
                renderer.clearScreen(clear);
                draw();
                renderer.endStripTarget();
                for (int i = 0; i < 16; ++i)
                  if (strip[i] != 0xa5 || stripEnd[i] != 0xa5) return false;
                std::copy(strip.begin() + 16, stripEnd, assembled.begin() + row * HalDisplay::DISPLAY_WIDTH_BYTES);
              }
              for (uint8_t byte : display.pixels)
                if (byte != 0x5a) return false;
            }
            const auto elapsedUs =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
            if (band == 0)
              reference[plane] = assembled;
            else if (reference[plane] != assembled) {
              size_t offset = 0;
              while (offset < assembled.size() && reference[plane][offset] == assembled[offset]) ++offset;
              std::cerr << "strip mismatch orientation=" << orientation << " plane=" << plane << " band=" << band
                        << " absolute=" << absolute << " offset=" << offset << '\n';
              return false;
            }
            std::cout << "cost\t" << orientation << ',' << placeholders << ',' << absolute << ',' << band << ','
                      << plane << '\t' << elapsedUs << ',' << prepareUs << ',' << stripStats.records << ','
                      << stripStats.drawn << ',' << stripStats.glyphs << ',' << prepareStats.records << ','
                      << prepareStats.glyphs << '\n';
            stripPhase = 0;
            std::cout << "pixels\t" << orientation << ',' << placeholders << ',' << absolute << ',' << band << ','
                      << plane << '\t' << hashBytes(assembled.data(), assembled.size()) << '\n';
            std::cout << "calls\t" << nativeCalls << ',' << emissions << ',' << scaledCalls;
            for (auto count : owners) std::cout << ',' << count;
            for (auto count : scanOwners) std::cout << ',' << count;
            std::cout << '\n';
            if (sample && orientation == config.orientation && !placeholders && !absolute && !band) {
              std::ofstream pbm("sample-" + std::to_string(plane) + ".pbm", std::ios::binary);
              pbm << "P4\n800 480\n";
              for (uint8_t byte : assembled) pbm.put(char(~byte));
            }
          }
        }
      }
    }
  }
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(config.orientation));
  return true;
}

#endif

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "PageGolden extracted-book config sd-font\n";
    return 2;
  }
  const auto fontPath = std::filesystem::absolute(argv[3]);
  std::filesystem::current_path(argv[1]);
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  builtinFonts(renderer);
  renderer.setFontVariantProvider(
      {&renderer, [](void*, int font, float scale) { return builtinReaderFontVariant(font, scale); },
       [](void* ctx, int font) { return static_cast<GfxRenderer*>(ctx)->getFontMap().count(font) != 0; }});
  SdCardFont sd;
  // Keep the firmware loader's bounded path short even for external evidence directories.
  std::filesystem::copy_file(fontPath, "font.cpfont", std::filesystem::copy_options::overwrite_existing);
  if (!sd.load("font.cpfont")) return 3;
  renderer.insertFont(100, EpdFontFamily(sd.getEpdFont(0), sd.getEpdFont(1), sd.getEpdFont(2), sd.getEpdFont(3)));
  renderer.registerSdCardFont(100, &sd);
  FontDecompressor decompressor;
  if (!decompressor.init()) return 4;
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  const auto config = setting(std::stoi(argv[2]), 100);
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(config.orientation));
  auto epub = std::make_shared<Epub>();
  std::vector<std::string> cssPaths;
  if (!readManifest(*epub, cssPaths)) return 5;
  std::filesystem::remove_all("cache");
  std::filesystem::create_directories("cache");
  CssParser css("cache");
  bool complete = true;
  for (const auto& path : cssPaths) {
    HalFile source;
    if (!source.open(("book/" + path).c_str(), "rb")) return 6;
    const auto result = css.loadFromStream(source);
    if (result == CssParser::ParseResult::Error) return 7;
    complete &= result == CssParser::ParseResult::Complete;
  }
  if (!css.saveToCache(complete)) return 8;
  epub->css = &css;
#ifdef XP_STRIP_IMAGES
  ImageBlock::setExtractor(nullptr, [](void*, const char* src, const char* dest) {
    std::error_code error;
    const auto source = std::filesystem::path("book") / src;
    if (!std::filesystem::is_regular_file(source)) return false;
    std::filesystem::create_directories(std::filesystem::path(dest).parent_path(), error);
    std::filesystem::copy_file(source, dest, std::filesystem::copy_options::overwrite_existing, error);
    return !error;
  });
#endif
  for (size_t spine = 0; spine < epub->spine.size(); ++spine) {
    Section section(epub, int(spine), renderer);
    if (!section.createSectionFile(config.spec) || !section.loadSectionFile(config.spec)) return 9;
    const std::string path = "cache/sections/" + std::to_string(spine) + ".bin";
    HalFile source;
    if (!source.open(path.c_str(), "rb")) return 10;
    XpSecRecord header;
    if (!sectioncodec::read(source, XP_SEC_HEADER, header)) return 11;
    std::vector<uint32_t> positions(section.pageCount + 1);
    if (!source.seek(header.values[15])) return 12;
    for (int i = 0; i < section.pageCount; ++i)
      if (!sectioncodec::readValue(source, XP_SEC_U32, positions[i])) return 13;
    positions.back() = header.values[15];
    std::cout << "section\t" << spine << '\t' << section.pageCount << '\n';
    for (int index = 0; index < section.pageCount; ++index) {
      ImageBlock::clearRenderFailures();
      ImageBlock::releaseRenderCache();
      auto page = section.loadPage(index);
      if (!page || positions[index + 1] < positions[index]) return 14;
      std::string raw(positions[index + 1] - positions[index], '\0');
      if (!source.seek(positions[index]) || source.read(raw.data(), raw.size()) != raw.size()) return 15;
      std::cout << "begin\t" << spine << '/' << index << '\n';
      std::cout << "raw\t" << hex(raw) << '\n';
      HalFile roundtrip("page.bin");
      if (!page->serialize(roundtrip)) return 16;
      roundtrip.close();
      std::cout << "wire\t" << hex(readBytes("page.bin")) << '\n';
      // Scan/prewarm before metric queries, as selection does for an already rendered page.
      {
        auto scope = cache.createPrewarmScope();
        page->render(renderer, config.spec.fontId, config.margin, config.margin);
        scope.endScanAndPrewarm();
        if (!records(*page, renderer, config)) return 17;
      }
#ifdef XP_STRIP_IMAGES
      // Decode once through the real converter, then measure pixel-cache playback.
      renderer.setRenderMode(GfxRenderer::BW);
      page->render(renderer, config.spec.fontId, config.margin, config.margin);
#endif
#ifdef XP_STRIP_SPIKE
      if (std::getenv("XP_STRIP_MATRIX")) {
        if (!stripPixels(*page, renderer, cache, display, config, false)) return 18;
      } else
#endif
          if (!pixels(*page, renderer, cache, display, config, spine == 0 && index == 0))
        return 18;
      std::cout << "end\n";
    }
  }
  ImageBlock::releaseRenderCache();
  return std::cout.good() ? 0 : 19;
}

#ifdef XP_STRIP_PLAN
struct PlanStorage {
  void* storage;
  size_t capacity_bytes, count;
  uint8_t valid;
};
extern "C" uint32_t xp_page_plan_render(const uint8_t*, size_t, const XpNativeProvider*, const XpPageRenderServices*,
                                        XpGlyphPaint*, int32_t, int32_t, int32_t, uint8_t, StripBounds*, size_t,
                                        PlanStorage*, uint8_t);
uint32_t pageGoldenPlan(const uint8_t* arena, size_t size, const XpNativeProvider* provider,
                        const XpPageRenderServices* services, XpGlyphPaint* paint, int32_t font, int32_t x, int32_t y,
                        uint8_t mode) {
  static uint64_t commands[16384 / sizeof(uint64_t)];
  static PlanStorage plan{commands, sizeof(commands), 0, 0};
  static bool faultsChecked = false;
  if (stripPhase == 1 && !faultsChecked && !stripBounds.empty()) {
    const auto attempt = [&](PlanStorage& probe, StripBounds* bounds, size_t count) {
      return xp_page_plan_render(arena, size, provider, services, paint, font, x, y, mode, bounds, count, &probe, 1);
    };
    PlanStorage probe{nullptr, sizeof(commands), 0, 1};
    if (attempt(probe, stripBounds.data(), stripBounds.size()) != 1 || probe.valid) std::abort();
    probe = {commands, sizeof(commands), 0, 1};
    if (attempt(probe, nullptr, stripBounds.size()) != 1 || probe.valid) std::abort();
    probe = {commands, sizeof(commands), 0, 1};
    if (attempt(probe, stripBounds.data(), 0) != 1 || probe.valid) std::abort();
    probe = {commands, 0, 0, 1};
    if (attempt(probe, stripBounds.data(), stripBounds.size()) != 2 || probe.valid) std::abort();
    faultsChecked = true;
  }
  const auto status = xp_page_plan_render(arena, size, provider, services, paint, font, x, y, mode, stripBounds.data(),
                                          stripBounds.size(), &plan, stripPhase == 1);
  if (status == 2 || (!plan.valid && stripPhase != 1)) {
    return xp_page_strip_render(arena, size, provider, services, paint, font, x, y, mode, stripBounds.data(),
                                stripBounds.size(), &stripStats, stripPhase == 1);
  }
  return status;
}
#endif
