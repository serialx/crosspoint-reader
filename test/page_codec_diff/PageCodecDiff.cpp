#include <Epub/Page.h>
#include <Epub/SectionCodec.h>
#include <GfxRenderer.h>
#include <xp_ffi.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <vector>

namespace allocation {
bool enabled = false;
struct Entry {
  void* pointer;
  size_t bytes;
};
Entry live[20000]{};
size_t requests = 0, bytes = 0, peak = 0, current = 0, remaining = 0;
size_t sizes[20000]{};
void reset() {
  std::memset(live, 0, sizeof(live));
  requests = bytes = peak = current = remaining = 0;
}
void added(void* p, size_t size) {
  if (!enabled) return;
  if (requests >= 20000) std::abort();
  sizes[requests++] = size;
  bytes += size;
  current += size;
  peak = std::max(peak, current);
  ++remaining;
  for (auto& entry : live)
    if (!entry.pointer) {
      entry = {p, size};
      return;
    }
  std::abort();
}
void removed(void* p) {
  if (!enabled) return;
  for (auto& entry : live)
    if (entry.pointer == p && p) {
      current -= entry.bytes;
      --remaining;
      entry = {};
      return;
    }
}
}  // namespace allocation
void* operator new(size_t size) {
  void* p = std::malloc(size ? size : 1);
  if (!p) throw std::bad_alloc();
  allocation::added(p, size);
  return p;
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept {
  allocation::removed(p);
  std::free(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return ::operator new(size, std::nothrow); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }

static size_t cases = 0, accepted = 0, fixturePages = 0, words = 0;
static void require(bool condition, const char* what) {
  if (!condition) {
    std::cerr << "case " << cases << ": " << what << '\n';
    std::exit(1);
  }
}
static bool equal(XpPageSpan a, const char* b, size_t n) {
  return a.len == n && (!n || std::memcmp(a.data, b, n) == 0);
}

static void compare(const Page& cpp, const std::vector<uint8_t>& arena, uint32_t visible) {
  XpPageInfo info{};
  uint32_t offset = 0;
  require(xp_page_info(arena.data(), arena.size(), &info, &offset) == 0, "page info");
  require(offset == visible && info.elements == cpp.elements.size(), "page counts/offset");
  require(info.footnotes == cpp.footnotes.size() && info.links == cpp.links.size(), "footer counts");
  int16_t actualBounds[4] = {}, expectedBounds[4] = {};
  uint8_t found = 0;
  require(xp_page_image_bounds(arena.data(), arena.size(), actualBounds, &found) == 0, "image bounds query");
  require(bool(found) ==
                  cpp.getImageBoundingBox(expectedBounds[0], expectedBounds[1], expectedBounds[2], expectedBounds[3]) &&
              bool(found) == cpp.hasImages(),
          "has images");
  require(!found || std::memcmp(actualBounds, expectedBounds, sizeof(actualBounds)) == 0, "image union bounds");
  for (size_t i = 0; i < cpp.elements.size(); ++i) {
    XpPageElement e{};
    require(xp_page_element(arena.data(), arena.size(), i, &e) == 0, "element query");
    const auto& c = *cpp.elements[i];
    require(e.tag == c.getTag() && int16_t(e.record.values[0]) == c.xPos && int16_t(e.record.values[1]) == c.yPos,
            "element position/tag");
    if (e.tag == TAG_PageLine) {
      const auto& block = *static_cast<const PageLine&>(c).getBlock();
      XpPageLine l{};
      require(xp_page_line(arena.data(), arena.size(), i, &l) == 0, "line query");
      require(l.count == block.wordCount() && bool(l.has_ruby) == block.hasRuby() && block.valid(),
              "line count/ruby/valid");
      const auto& s = block.getBlockStyle();
      const uint32_t style[] = {uint32_t(s.alignment),    uint32_t(s.textAlignDefined), uint16_t(s.marginTop),
                                uint16_t(s.marginBottom), uint16_t(s.marginLeft),       uint16_t(s.marginRight),
                                uint16_t(s.paddingTop),   uint16_t(s.paddingBottom),    uint16_t(s.paddingLeft),
                                uint16_t(s.paddingRight), uint16_t(s.textIndent),       uint32_t(s.textIndentDefined),
                                uint32_t(s.isRtl),        uint32_t(s.directionDefined), uint8_t(s.characterSpacing),
                                uint32_t(s.fontId)};
      for (size_t field = 0; field < 16; ++field) require(l.block.values[field] == style[field], "block style");
      for (uint16_t j = 0; j < l.count; ++j) {
        XpPageWord w{};
        require(xp_page_word(arena.data(), arena.size(), i, j, 24, &w) == 0, "word query");
        require(equal(w.text, block.wordText(j), block.wordTextLen(j)) && w.text.data[w.text.len] == 0, "word bytes");
        const auto source = block.wordSourceRange(j);
        require(w.source_start == source.start && w.source_end == source.end, "source range");
        require(w.x == block.wordXpos(j) && w.style == block.wordStyle(j) && w.font == block.wordFontId(j, 24),
                "word position/style/font");
        require(bool(w.starts_paragraph) == block.wordStartsParagraph(j) &&
                    bool(w.discretionary_hyphen) == block.wordHasDiscretionaryHyphen(j) &&
                    bool(w.small_caps) == block.wordHasSmallCaps(j),
                "word flags");
        require(w.focus_boundary == block.focusBoundary(j) && w.focus_suffix == block.focusSuffixX(j), "focus arrays");
        const auto& ruby = block.getRubyTexts();
        const std::string empty;
        const auto& text = j < ruby.size() ? ruby[j] : empty;
        require(equal(w.ruby, text.data(), text.size()), "ruby bytes");
        ++words;
      }
      XpPageWord invalid{};
      require(xp_page_word(arena.data(), arena.size(), i, l.count, 24, &invalid) == 3, "word index bound");
    } else if (e.tag == TAG_PageImage) {
      const auto& block = static_cast<const PageImage&>(c).getImageBlock();
      require(equal(e.first, block.getImagePath().data(), block.getImagePath().size()) && e.width == block.getWidth() &&
                  e.height == block.getHeight(),
              "image fields");
    } else if (e.tag == TAG_PageDropCap) {
      const auto* text = static_cast<const PageDropCap&>(c).getText();
      require(equal(e.first, text, std::strlen(text)), "drop cap text");
    }
  }
  for (size_t i = 0; i < cpp.footnotes.size(); ++i) {
    XpPageSpan number{}, href{};
    require(xp_page_footnote(arena.data(), arena.size(), i, &number, &href) == 0, "footnote query");
    require(equal(number, cpp.footnotes[i].number, 32) && equal(href, cpp.footnotes[i].href, 256), "footnote bytes");
  }
  for (size_t i = 0; i < cpp.links.size(); ++i) {
    XpPageLink link{};
    require(xp_page_link(arena.data(), arena.size(), i, &link) == 0, "link query");
    const auto& c = cpp.links[i];
    require(equal(link.href, c.href, 256) && link.x == c.x && link.y == c.y && link.width == c.width &&
                link.height == c.height,
            "link fields");
  }
}

static void rebuild(const Page& cpp, const std::vector<uint8_t>& arena, const std::vector<uint8_t>& canonical) {
  std::vector<uint8_t> output(canonical.size());
  XpPageBuilder b{};
  require(xp_page_builder_init(&b, output.data(), output.size()) == 0, "builder init");
  for (size_t i = 0; i < cpp.elements.size(); ++i) {
    XpPageElement e{};
    require(xp_page_element(arena.data(), arena.size(), i, &e) == 0, "builder element query");
    if (e.tag == TAG_PageLine) {
      const auto& block = *static_cast<const PageLine&>(*cpp.elements[i]).getBlock();
      XpPageLine query{};
      require(xp_page_line(arena.data(), arena.size(), i, &query) == 0, "builder line query");
      const auto n = block.wordCount();
      std::vector<uint8_t> text, styles, boundaries, slots;
      std::vector<int16_t> positions;
      std::vector<uint16_t> offsets, suffixes;
      std::vector<uint32_t> sources;
      std::vector<XpParaSpan> ruby;
      const auto& s = block.getBlockStyle();
      XpParaBlock style{};
      style.alignment = uint8_t(s.alignment);
      style.character_spacing = s.characterSpacing;
      style.margin_top = s.marginTop;
      style.margin_bottom = s.marginBottom;
      style.margin_left = s.marginLeft;
      style.margin_right = s.marginRight;
      style.padding_top = s.paddingTop;
      style.padding_bottom = s.paddingBottom;
      style.padding_left = s.paddingLeft;
      style.padding_right = s.paddingRight;
      style.text_indent = s.textIndent;
      style.text_indent_defined = s.textIndentDefined;
      style.text_align_defined = s.textAlignDefined;
      style.rtl = s.isRtl;
      style.direction_defined = s.directionDefined;
      style.font_id = s.fontId;
      for (uint16_t j = 0; j < n; ++j) {
        XpPageWord w{};
        xp_page_word(arena.data(), arena.size(), i, j, 24, &w);
        offsets.push_back(text.size());
        text.insert(text.end(), w.text.data, w.text.data + w.text.len + 1);
        positions.push_back(w.x);
        styles.push_back((w.style & 127) | (w.discretionary_hyphen ? 128 : 0));
        sources.push_back(w.source_start);
        sources.push_back(w.source_end);
        if (query.has_focus) {
          boundaries.push_back(w.focus_boundary);
          suffixes.push_back(w.focus_suffix);
        }
        ruby.push_back({w.ruby.data, uint32_t(w.ruby.len)});
        uint8_t slot = w.small_caps ? 128 : 0;
        if (w.font != block.blockFontId(24)) {
          for (int k = 0; k < 3; ++k)
            if (query.fonts[k] == w.font) {
              slot |= k + 1;
              break;
            }
        }
        slots.push_back(slot);
      }
      // Preserve original font slots, including an explicit slot equal to the block font.
      // They are compared separately through the production serialization below.
      std::vector<uint8_t> serialized;
      HalFile blockFile(serialized);
      block.serialize(blockFile);
      if (query.has_fonts && n) std::memcpy(slots.data(), serialized.data() + serialized.size() - n, n);
      XpParaLine line{};
      line.text = {text.data(), uint32_t(text.size())};
      line.offsets = offsets.data();
      line.count = n;
      line.positions = positions.data();
      line.styles = styles.data();
      line.boundaries = query.has_focus ? boundaries.data() : nullptr;
      line.suffixes = suffixes.data();
      line.sources = sources.data();
      line.ruby = ruby.data();
      line.block = &style;
      line.paragraph_start = query.paragraph_start;
      line.font_slots = query.has_fonts ? slots.data() : nullptr;
      line.fonts = query.fonts;
      require(xp_page_builder_line(&b, cpp.elements[i]->xPos, cpp.elements[i]->yPos, &line) == 0, "builder line");
    } else {
      if (e.tag == TAG_PageImage) {
        e.record.kind = XP_SEC_IMAGE_SIZE;
        e.record.values[0] = uint16_t(e.width);
        e.record.values[1] = uint16_t(e.height);
      }
      require(
          xp_page_builder_element(&b, cpp.elements[i]->xPos, cpp.elements[i]->yPos, &e.record, e.first, e.second) == 0,
          "builder append");
    }
  }
  for (const auto& note : cpp.footnotes)
    require(xp_page_builder_footnote(&b, reinterpret_cast<const uint8_t*>(note.number),
                                     reinterpret_cast<const uint8_t*>(note.href)) == 0,
            "builder footnote");
  for (const auto& c : cpp.links) {
    XpPageLink link{{reinterpret_cast<const uint8_t*>(c.href), 256}, c.x, c.y, c.width, c.height};
    require(xp_page_builder_link(&b, &link) == 0, "builder link");
  }
  size_t written = 0;
  require(xp_page_builder_finish(&b, &written) == 0 && written == canonical.size() && output == canonical,
          "builder exact bytes");
}

static bool check(const std::vector<uint8_t>& bytes, bool fixture, uint32_t visible, std::ostream* memory) {
  ++cases;
  HalFile input(bytes.data(), bytes.size());
  if (fixture) allocation::reset();
  allocation::enabled = fixture;
  auto cpp = Page::deserialize(input);
  allocation::enabled = false;
  XpPageInfo info{};
  const auto status = xp_page_arena_size(bytes.data(), bytes.size(), &info);
  require(bool(cpp) == (status == 0), "accept/reject mismatch");
  if (!cpp) return false;
  ++accepted;
  require(input.position() == info.consumed, "consumed byte count");
  std::vector<uint8_t> arena(info.arena_size);
  require(xp_page_decode(bytes.data(), bytes.size(), visible, arena.data(), arena.size(), &info) == 0, "decode");
  compare(*cpp, arena, visible);
  std::vector<uint8_t> canonical;
  HalFile output(canonical);
  require(cpp->serialize(output), "C++ reserialize");
  size_t size = 0;
  require(xp_page_encode(arena.data(), arena.size(), nullptr, 0, &size) == 2, "encode sizing");
  std::vector<uint8_t> encoded(size);
  require(
      xp_page_encode(arena.data(), arena.size(), encoded.data(), encoded.size(), &size) == 0 && encoded == canonical,
      "canonical re-encode");
  if (fixture) {
    require(canonical == bytes, "fixture is not canonical");
    rebuild(*cpp, arena, canonical);
    ++fixturePages;
    if (memory) {
      *memory << fixturePages << ',' << bytes.size() << ',' << arena.size() << ',' << allocation::requests << ','
              << allocation::bytes << ',' << allocation::peak << ',' << allocation::remaining << ','
              << allocation::current << ",\"";
      std::map<size_t, size_t> histogram;
      for (size_t i = 0; i < allocation::requests; ++i) ++histogram[allocation::sizes[i]];
      for (const auto& [request, count] : histogram) *memory << request << ':' << count << ';';
      size_t elementBytes = 0, textObjects = 0, wordArenas = 0, fontBytes = 0, rubyVectors = 0, imageObjects = 0;
      size_t lineCount = 0, fontCount = 0, rubyCount = 0, imageCount = 0;
      for (size_t i = 0; i < cpp->elements.size(); ++i) {
        switch (cpp->elements[i]->getTag()) {
          case TAG_PageLine: {
            elementBytes += sizeof(PageLine);
            textObjects += sizeof(TextBlock);
            ++lineCount;
            XpPageLine l{};
            xp_page_line(arena.data(), arena.size(), i, &l);
            if (l.count) {
              const auto& block = *static_cast<const PageLine&>(*cpp->elements[i]).getBlock();
              const auto last = l.count - 1;
              wordArenas += l.count * (13 + (l.has_focus ? 3 : 0)) +
                            (block.wordText(last) + block.wordTextLen(last) + 1 - block.wordText(0));
            }
            if (l.has_fonts) {
              ++fontCount;
              fontBytes += 12 + l.count;
            }
            if (l.has_ruby) {
              ++rubyCount;
              rubyVectors += l.count * sizeof(std::string);
            }
            break;
          }
          case TAG_PageImage:
            elementBytes += sizeof(PageImage);
            imageObjects += sizeof(ImageBlock);
            ++imageCount;
            break;
          case TAG_PageHorizontalRule:
            elementBytes += sizeof(PageHorizontalRule);
            break;
          case TAG_PageDropCap:
            elementBytes += sizeof(PageDropCap);
            break;
          case TAG_PageBorderBox:
            elementBytes += sizeof(PageBorderBox);
            break;
        }
      }
      const size_t vectors = cpp->elements.capacity() * sizeof(std::unique_ptr<PageElement>) +
                             cpp->footnotes.capacity() * sizeof(FootnoteEntry) +
                             cpp->links.capacity() * sizeof(PageLink);
      const size_t classified =
          sizeof(Page) + elementBytes + textObjects + wordArenas + fontBytes + rubyVectors + imageObjects + vectors;
      require(classified <= allocation::current, "memory classification");
      *memory << "\"," << sizeof(Page) << ',' << elementBytes << ',' << textObjects << ',' << wordArenas << ','
              << fontBytes << ',' << rubyVectors << ',' << imageObjects << ',' << vectors << ','
              << allocation::current - classified << ',' << lineCount << ',' << fontCount << ',' << rubyCount << ','
              << imageCount << '\n';
    }
  }
  return true;
}

static std::vector<uint8_t> synthetic() {
  Page p;
  BlockStyle style;
  style.fontId = 26;
  style.marginLeft = -3;
  style.characterSpacing = -2;
  style.isRtl = true;
  std::vector<TextBlock::SourceRange> sources{{2, 5}, {9, 12}};
  auto block =
      std::make_unique<TextBlock>(std::vector<std::string>{"한글", "two"}, std::vector<int16_t>{-8, 40},
                                  std::vector<EpdFontFamily::Style>{EpdFontFamily::Style(128), EpdFontFamily::BOLD},
                                  std::vector<uint8_t>{3, 0}, std::vector<uint16_t>{20, 0}, style,
                                  std::vector<std::string>{"ruby", ""}, std::vector<TextBlock::LinkSpan>{}, sources, 1);
  const int32_t fonts[3] = {32, 24, 0};
  const uint8_t slots[] = {129, 2};
  block->setWordFonts(slots, fonts);
  p.elements.push_back(std::make_unique<PageLine>(std::move(block), -5, 20));
  p.elements.push_back(
      std::make_unique<PageImage>(std::make_unique<ImageBlock>("cache/a.png", "image/a.png", -3, 90), 10, 20));
  p.elements.push_back(std::make_unique<PageHorizontalRule>(500, 2, -3, 35));
  p.elements.push_back(std::make_unique<PageDropCap>(28, 600, EpdFontFamily::BOLD, "A한", -2, 80));
  CssBorderSide sides[] = {{1, CssBorderStyle::Solid},
                           {2, CssBorderStyle::Double},
                           {3, CssBorderStyle::Dashed},
                           {4, CssBorderStyle::Dotted}};
  p.elements.push_back(std::make_unique<PageBorderBox>(300, 100, sides, true, -2, 99));
  p.addFootnote("1", "#note");
  p.addLink("#note", 4, 5, 20, 30);
  // FootnoteEntry deliberately leaves its tail unspecified; initialize it for a reproducible fixture.
  std::memset(p.footnotes[0].number + 2, 0, 30);
  std::memset(p.footnotes[0].href + 6, 0, 250);
  std::vector<uint8_t> bytes;
  HalFile out(bytes);
  require(p.serialize(out), "synthetic serialize");
  return bytes;
}
static void arenaContracts(const std::vector<uint8_t>& bytes) {
  XpPageInfo info{};
  require(xp_page_arena_size(nullptr, 1, &info) == 3, "null input");
  require(xp_page_arena_size(bytes.data(), bytes.size(), &info) == 0, "contract sizing");
  std::vector<uint8_t> arena(info.arena_size + 262, 0xa5);
  require(xp_page_decode(bytes.data(), bytes.size(), 5, arena.data(), info.arena_size - 1, &info) == 2, "short arena");
  require(std::all_of(arena.begin(), arena.end(), [](uint8_t b) { return b == 0xa5; }), "atomic short decode");
  require(xp_page_decode(bytes.data(), bytes.size(), 5, arena.data(), arena.size(), &info) == 0, "contract decode");
  XpParaLink span{};
  std::memcpy(span.href, "#span", 6);
  span.x = -3;
  span.width = 12;
  span.top_lift = 4;
  size_t required = 0;
  require(
      xp_page_attach_line_links(arena.data(), info.arena_size, 0, &span, 1, &required) == 2 && required == arena.size(),
      "short span storage");
  require(xp_page_attach_line_links(arena.data(), arena.size(), 0, &span, 1, &required) == 0, "span storage");
  XpPageLineLink view{};
  size_t count = 0;
  require(xp_page_line_link(arena.data(), arena.size(), 0, 0, &view, &count) == 0 && count == 1, "span query");
  require(equal(view.href, span.href, 256) && view.x == -3 && view.width == 12 && view.top_lift == 4, "span fields");
  std::vector<uint8_t> encoded(bytes.size());
  require(
      xp_page_encode(arena.data(), arena.size(), encoded.data(), encoded.size(), &required) == 0 && encoded == bytes,
      "layout metadata omitted");
}

static void mutations(const std::vector<uint8_t>& page) {
  for (size_t cut = 0; cut < page.size(); ++cut) check({page.begin(), page.begin() + cut}, false, 0, nullptr);
  for (size_t at = 0; at < page.size(); ++at) {
    for (uint8_t bit : {uint8_t(1), uint8_t(128)}) {
      auto mutant = page;
      mutant[at] ^= bit;
      check(mutant, false, 0, nullptr);
    }
  }
  // Every position is considered a possible 16/32-bit field, including length
  // and offset boundaries not reached after a mutated discriminator.
  for (size_t at = 0; at < page.size(); ++at) {
    for (uint32_t value : {0u, 1u, 16u, 17u, 32u, 33u, 10000u, 10001u, 65535u, 0x7fffffffu, 0x80000000u, 0xffffffffu}) {
      for (size_t width : {2u, 4u}) {
        if (at + width > page.size()) continue;
        auto mutant = page;
        for (size_t j = 0; j < width; ++j) mutant[at + j] = uint8_t(value >> (j * 8));
        check(mutant, false, 0, nullptr);
      }
    }
  }
}
int main(int argc, char** argv) {
  require(argc >= 2, "usage: PageCodecDiff corpus [memory.csv]");
  std::ofstream memory;
  if (argc > 2) {
    memory.open(argv[2]);
    memory << "page,wire_bytes,rust_arena_bytes,cpp_allocations,cpp_requested_bytes,cpp_peak_bytes,cpp_live_"
              "allocations,cpp_live_bytes,request_size_counts,page_object_bytes,element_object_bytes,text_object_bytes,"
              "word_arena_bytes,font_bytes,ruby_vector_bytes,image_object_bytes,vector_bytes,string_bytes,line_count,"
              "font_count,ruby_count,image_count\n";
  }
  const auto sample = synthetic();
  arenaContracts(sample);
  check(sample, true, 987, memory ? &memory : nullptr);
  mutations(sample);
  std::ifstream input(argv[1], std::ios::binary);
  require(bool(input), "open corpus");
  size_t pages = 0;
  while (input.peek() != EOF) {
    uint32_t size = 0, visible = 0;
    input.read(reinterpret_cast<char*>(&size), 4);
    input.read(reinterpret_cast<char*>(&visible), 4);
    require(size < 16 * 1024 * 1024 && bool(input), "corpus record size");
    std::vector<uint8_t> bytes(size);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    require(bool(input), "corpus record");
    require(check(bytes, true, visible, memory ? &memory : nullptr), "fixture rejected");
    if (pages < 8) mutations(bytes);
    ++pages;
  }
  std::cout << "{\"fixtures\":" << fixturePages << ",\"cases\":" << cases << ",\"accepted\":" << accepted
            << ",\"word_queries\":" << words << ",\"sizeof_page\":" << sizeof(Page)
            << ",\"sizeof_line\":" << sizeof(PageLine) << ",\"sizeof_text_block\":" << sizeof(TextBlock)
            << ",\"sizeof_image_block\":" << sizeof(ImageBlock) << "}\n";
}
