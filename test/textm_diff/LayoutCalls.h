#pragma once
#include <xp_ffi.h>

#include <iostream>

#include "Fixtures.h"
#include "Renderer.h"
// Exercise the production layout engine and count its remaining metric callbacks.
inline int layoutCalls(GfxRenderer& renderer) {
  struct Counts {
    GfxRenderer* renderer;
    uint64_t advance = 0, space = 0, kern = 0, spaceAdvance = 0, ascender = 0, prepare = 0, lines = 0;
  } count{&renderer};
  XpParaMetrics metrics{&count,
                        nullptr,
                        [](void* c, int32_t f, XpParaSpan text, uint8_t s, int8_t t) -> int32_t {
                          auto& n = *static_cast<Counts*>(c);
                          ++n.advance;
                          return n.renderer->getTextAdvanceX(
                              f, reinterpret_cast<const char*>(text.data), static_cast<EpdFontFamily::Style>(s), t,
                              BidiUtils::BidiBaseDir::LTR, GfxRenderer::TextMeasureMode::Layout);
                        },
                        [](void* c, int32_t f, uint8_t s) -> int32_t {
                          auto& n = *static_cast<Counts*>(c);
                          ++n.space;
                          return n.renderer->getSpaceWidth(f, static_cast<EpdFontFamily::Style>(s));
                        },
                        [](void* c, int32_t f, uint32_t a, uint32_t b, uint8_t s, int8_t t) -> int32_t {
                          auto& n = *static_cast<Counts*>(c);
                          ++n.kern;
                          return n.renderer->getKerning(f, a, b, static_cast<EpdFontFamily::Style>(s), t);
                        },
                        [](void* c, int32_t f, uint32_t a, uint32_t b, uint8_t s) -> int32_t {
                          auto& n = *static_cast<Counts*>(c);
                          ++n.spaceAdvance;
                          return n.renderer->getSpaceAdvance(f, a, b, static_cast<EpdFontFamily::Style>(s));
                        },
                        [](void* c, int32_t f) -> int32_t {
                          auto& n = *static_cast<Counts*>(c);
                          ++n.ascender;
                          return n.renderer->getFontAscenderSize(f);
                        },
                        [](void*, int32_t) -> uint8_t { return 0; },
                        [](void* c, int32_t, const XpParaSpan*, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t) {
                          ++static_cast<Counts*>(c)->prepare;
                        },
                        [](XpParaSpan, int32_t) -> uint8_t { return 0; },
                        [](XpParaSpan, const uint16_t*, uint32_t, uint8_t, uint16_t*) -> uint8_t { return 0; }};
  const XpParaAllocator allocator{[](size_t bytes, uint8_t) { return std::malloc(bytes); }, std::free};
  constexpr const char* words[] = {"office",      "AVATAR",     "discretionary", "internationalization",
                                   "measurement", "rendering",  "reader",        "a",
                                   "font",        "hyphenation"};
  for (uint8_t focus : {0, 1})
    for (uint8_t hyphen : {0, 1})
      for (uint16_t width : {80, 240}) {
        count = Counts{&renderer};
        for (int paragraph = 0; paragraph < 100; ++paragraph) {
          alignas(uintptr_t) uintptr_t storage[64]{};
          auto* state = reinterpret_cast<XpParagraph*>(storage);
          XpParaBlock block{};
          block.font_scale = 1.0f;
          xp_para_init(state, &allocator, &block, hyphen, focus, 0, metrics.starts_rtl);
          uint32_t offset = 0;
          for (int i = 0; i < 20; ++i) {
            const char* word = words[(paragraph + i) % std::size(words)];
            uint32_t size = std::strlen(word);
            xp_para_add_word(state, {reinterpret_cast<const uint8_t*>(word), size}, 0, 0, 0, offset, 0, 0, 0);
            offset += size + 1;
          }
          XpParaLayout options{
              1, width, 1, 0, 100, UINT32_MAX, 0, nullptr, &count, [](void* c, const XpParaLine*) -> uint8_t {
                ++static_cast<Counts*>(c)->lines;
                return 1;
              }};
          xp_para_layout(state, &metrics, &options);
          require(!xp_para_dropped(state), "layout-count OOM");
          xp_para_destroy(state);
        }
        std::cout << "words=2000 focus=" << int(focus) << " hyphen=" << int(hyphen) << " width=" << width
                  << " advance=" << count.advance << " advance/word=" << double(count.advance) / 2000
                  << " space=" << count.space << " kern=" << count.kern << " space_advance=" << count.spaceAdvance
                  << " ascender=" << count.ascender << " prepare=" << count.prepare << " lines=" << count.lines << '\n';
      }
  return 0;
}
