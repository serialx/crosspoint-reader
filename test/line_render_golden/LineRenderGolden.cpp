#define main pageGoldenMain
#include "../page_golden/PageGolden.cpp"
#undef main
#include <BidiUtils.h>
#include <Epub/Paragraph.h>
#include <HalMemory.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <xp_ui_dictionary.h>

#include <chrono>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include "Allocations.h"
#include "CrossPointSettings.h"
#include "activities/settings/TextSettingsPreview.h"

constexpr char DICTIONARY_PLAIN_HEADWORD_MARKER = '\1';
struct DictionaryDefinitionActivity {
  struct PlainSelectionLine;
  struct Line {
    uint32_t start;
    uint16_t len;
    bool bold;
  };
  struct Definition {
    std::string text;
    std::vector<Line> lines;
    int totalPages = 0, selectionPage = 0, linesPerPage = 5;
  } content;
  struct Lookup {
    std::vector<std::unique_ptr<Page>> selectionPages;
  } storage;
  Lookup* lookup = &storage;
  GfxRenderer& renderer;
  int measureSpan(int font, const char* bytes, size_t n, EpdFontFamily::Style family = EpdFontFamily::REGULAR) const;
  bool buildPlainSelectionPages();
};
#include "Consumers.inc"

static void frame(const std::string& key, const HalDisplay& display) {
  std::cout << "frame\t" << key << '\n';
  std::cout.write(reinterpret_cast<const char*>(display.pixels.data()), display.pixels.size());
}
static void geometry(const ClipSelectionActivity& clip) {
  std::cout << "geometry\t" << clip.wordCount << '\n';
  for (size_t i = 0; i < clip.wordCount; ++i) {
    const auto& w = clip.words[i];
    std::cout << "word\t" << w.x << ',' << w.y << ',' << w.width << ',' << w.height << ',' << w.row << ','
              << int(w.pageOffset) << ',' << w.pageWordIndex << ',' << w.fontId << ',' << w.startOffset << ','
              << w.endOffset << ',' << int(w.paragraphStart) << ',' << int(w.isRtl) << ',' << int(w.discretionaryHyphen)
              << '\t' << hex(w.text) << '\n';
  }
}
struct RenderCost {
  double micros, instructions;
};
static uint64_t instructions() {
#ifdef __APPLE__
  rusage_info_v4 usage{};
  if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&usage)))
    return usage.ri_instructions;
#endif
  return 0;
}
static void metrics(const char* name, linealloc::Stats stats, RenderCost cost) {
  std::cerr << name << " requests=" << stats.requests << " peak_bytes=" << stats.peak
            << " largest_bytes=" << stats.largest << " resident_bytes=" << stats.live << " render_us=" << cost.micros
            << " render_instructions=" << cost.instructions << '\n';
}
template <class Draw>
static RenderCost timing(Draw draw) {
  std::array<double, 7> samples;
  std::array<double, 7> counts;
  for (size_t n = 0; n < samples.size(); ++n) {
    const uint64_t before = instructions();
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) draw();
    samples[n] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 100;
    const uint64_t after = instructions();
    counts[n] = before && after >= before ? double(after - before) / 100 : 0;
  }
  std::sort(samples.begin(), samples.end());
  std::sort(counts.begin(), counts.end());
  return {samples[3], counts[3]};
}
static int resources(GfxRenderer& renderer) {
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.setOrientation(GfxRenderer::Portrait);
  SETTINGS = {};
  for (int focus = 0; focus < 2; ++focus) {
    SETTINGS.focusReadingEnabled = focus;
    textsettings::PreviewLayout layout;
    linealloc::begin();
    textsettings::renderPreview(renderer, layout, 8, 4, {7, 11, 466, 280}, "Preview");
    const auto stats = linealloc::end();
    const auto us = timing([&] { textsettings::renderPreview(renderer, layout, 8, 4, {7, 11, 466, 280}, "Preview"); });
    metrics(focus ? "preview-focus" : "preview", stats, us);
  }
  DictionaryDefinitionActivity owner{{}, {}, nullptr, renderer};
  owner.lookup = &owner.storage;
  for (int row = 0; row < 30; ++row) {
    const auto start = owner.content.text.size();
    owner.content.text += "The dictionary definition contains words, punctuation, e\xcc\x81 and a longerword.";
    owner.content.lines.push_back({uint32_t(start), uint16_t(owner.content.text.size() - start), false});
    owner.content.text += '\n';
  }
  owner.content.linesPerPage = 10;
  owner.content.totalPages = 3;
  linealloc::begin();
  if (!owner.buildPlainSelectionPages()) return 20;
  const auto stats = linealloc::end();
  const auto us = timing([&] { owner.storage.selectionPages[0]->render(renderer, SETTINGS.font, 12, 12); });
  metrics("dictionary", stats, us);
  return 0;
}

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  builtinFonts(renderer);
  FontDecompressor decompressor;
  if (!decompressor.init()) return 2;
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  if (argc > 1 && std::string(argv[1]) == "--bench") return resources(renderer);
  const int fonts[] = {NOTOSANS_12_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_18_FONT_ID};
  const float compression[] = {0.90f, 0.95f, 1.0f, 1.05f};
  for (int font = 0; font < (quick ? 1 : 4); ++font)
    for (int space = 0; space < (quick ? 1 : 4); ++space)
      for (int align = 0; align < (quick ? 1 : 5); ++align)
        for (int flags = 0; flags < 8; ++flags)
          for (int orientation = 0; orientation < 4; ++orientation) {
            SETTINGS.font = fonts[font];
            SETTINGS.fontPointSize = BUILTIN_READER_POINT_SIZES[font];
            SETTINGS.compression = compression[space];
            SETTINGS.paragraphAlignment = align;
            SETTINGS.hyphenationEnabled = flags & 1;
            SETTINGS.focusReadingEnabled = flags & 2;
            SETTINGS.extraParagraphSpacing = flags & 4;
            renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
            textsettings::PreviewLayout layout;
            const Rect bounds{7, 11, renderer.getScreenWidth() - 14, 280};
            for (int plane = 0; plane < 3; ++plane) {
              renderer.setRenderMode(static_cast<GfxRenderer::RenderMode>(plane));
              renderer.clearScreen(plane == 0 ? 0xff : 0);
              textsettings::renderPreview(renderer, layout, 8, 4, bounds, "Preview");
              frame("preview/" + std::to_string(font) + "/" + std::to_string(space) + "/" + std::to_string(align) +
                        "/" + std::to_string(flags) + "/" + std::to_string(orientation) + "/" + std::to_string(plane),
                    display);
            }
          }
  for (const char* sample : {"A long example with hyphenation extraordinarily useful for reading.",
                             "שלום עולם العربية Latin", "中文 日本語 한글 e\xcc\x81 marks"})
    for (int height : {35, 75, 113}) {
      I18N.sample = sample;
      textsettings::PreviewLayout layout;
      for (int plane = 0; plane < 3; ++plane) {
        renderer.setRenderMode(static_cast<GfxRenderer::RenderMode>(plane));
        renderer.clearScreen(plane == 0 ? 0xff : 0);
        textsettings::renderPreview(renderer, layout, 8, 4, {7, 11, 250, height}, "Preview");
        frame("preview-edge/" + hex(sample) + "/" + std::to_string(height) + "/" + std::to_string(plane), display);
      }
    }
  const char* definitions[] = {"A dictionary entry with several words and punctuation.",
                               "中文 日本語 한글 e\xcc\x81 marks",
                               "שלום עולם العربية mixed Latin 123",
                               "supercalifragilisticexpialidocious a-very-long-unbreakable-word",
                               "\1Headword: bold and italic runs are exercised by rich pages.",
                               "\t  repeated   spaces and\t tabs"};
  SETTINGS.font = NOTOSANS_14_FONT_ID;
  for (int fixture = 0; fixture < 6; ++fixture) {
    DictionaryDefinitionActivity owner{{}, {}, nullptr, renderer};
    owner.lookup = &owner.storage;
    for (int row = 0; row < 14; ++row) {
      const auto start = owner.content.text.size();
      owner.content.text += definitions[(fixture + row) % 6];
      const auto len = owner.content.text.size() - start;
      owner.content.lines.push_back({uint32_t(start), uint16_t(len), row == 0});
      owner.content.text += '\n';
    }
    owner.content.totalPages = 3;
    if (!owner.buildPlainSelectionPages()) return 3;
    ClipSelectionActivity clip{renderer, owner.lookup->selectionPages, ClipSelectionActivity::Mode::DefinitionLookup,
                               SETTINGS.font};
    if (!clip.extractWords()) return 4;
    geometry(clip);
    for (size_t p = 0; p < clip.pageCount(); ++p)
      for (int orientation = 0; orientation < 4; ++orientation)
        for (int plane = 0; plane < 3; ++plane)
          for (int highlight = 0; highlight < 2; ++highlight) {
            renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
            renderer.setRenderMode(static_cast<GfxRenderer::RenderMode>(plane));
            renderer.clearScreen(plane == 0 ? 0xff : 0);
            clip.currentPageOffset = p;
            clip.pageAt(p).render(renderer, SETTINGS.font, 12, 12);
            if (highlight) clip.drawSelection(0, clip.wordCount - 1);
            frame("dictionary/" + std::to_string(fixture) + "/" + std::to_string(p) + "/" +
                      std::to_string(orientation) + "/" + std::to_string(plane) + "/" + std::to_string(highlight),
                  display);
          }
  }
  // Build cached book-style pages before exercising the same selection consumer.
  for (int fixture = 0; fixture < 4; ++fixture) {
    Page owned;
    BlockStyle style;
    style.alignment = static_cast<CssTextAlign>(fixture);
    Paragraph paragraph(true, fixture == 1, style, 2);
    uint32_t offset = 0;
    for (const char* word : {"Book", "bold", "italic", "underline", "strike", "superscript", "subscript", "focus",
                             "e\xcc\x81", "שלום", "العربية", "日本語", "漢字", "more", "text"}) {
      static constexpr EpdFontFamily::Style styles[] = {
          EpdFontFamily::REGULAR,       EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::UNDERLINE,
          EpdFontFamily::STRIKETHROUGH, EpdFontFamily::SUP,  EpdFontFamily::SUB};
      paragraph.addWord(word, styles[offset % 7], false, false, offset);
      offset += strlen(word) + 1;
    }
    paragraph.setRubyForWordAt(12, "かんじ");
    int y = 0;
    paragraph.layoutAndExtractLines(renderer, SETTINGS.font, 220, [&](std::unique_ptr<TextBlock> line, uint32_t) {
      owned.elements.push_back(std::make_unique<PageLine>(std::move(line), 0, y));
      y += 48;
    });
    HalFile output("rich-page.bin");
    if (!owned.serialize(output)) return 5;
    output.close();
    const auto bytes = readBytes("rich-page.bin");
    std::vector<std::unique_ptr<Page>> pages;
    pages.push_back(Page::fromBytes(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
    if (!pages.back()) return 6;
    ClipSelectionActivity clip{renderer, pages, ClipSelectionActivity::Mode::Clip, SETTINGS.font};
    if (!clip.extractWords()) return 7;
    geometry(clip);
    for (int orientation = 0; orientation < 4; ++orientation)
      for (int plane = 0; plane < 3; ++plane) {
        renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
        renderer.setRenderMode(static_cast<GfxRenderer::RenderMode>(plane));
        renderer.clearScreen(plane == 0 ? 0xff : 0);
        clip.pageAt(0).render(renderer, SETTINGS.font, 12, 12);
        clip.drawSelection(1, clip.wordCount - 1);
        frame("book-clip/" + std::to_string(fixture) + "/" + std::to_string(orientation) + "/" + std::to_string(plane),
              display);
      }
  }
  return 0;
}
