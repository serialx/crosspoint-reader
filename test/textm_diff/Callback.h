#pragma once
#include <xp_ffi.h>

#include <array>

#include "Renderer.h"
struct Callback {
  const EpdFontFamily* family;
  SdCardFont* sd;
  std::array<uint64_t, 9> calls{};
  XpTextmFace face() {
    return {this,
            [](void* c, uint32_t cp, uint8_t s, XpTextmGlyph* out) -> uint8_t {
              auto& f = *static_cast<Callback*>(c);
              ++f.calls[0];
              const auto* g = f.family->getGlyph(cp, static_cast<EpdFontFamily::Style>(s));
              if (!g) return 0;
              *out = {g->advanceX, g->width, g->height, g->left, g->top};
              return 1;
            },
            [](void* c, uint32_t cp, uint8_t s) -> uint8_t {
              auto& f = *static_cast<Callback*>(c);
              ++f.calls[1];
              return f.family->hasCodepoint(cp, static_cast<EpdFontFamily::Style>(s));
            },
            [](void* c, uint32_t a, uint32_t b, uint8_t s) -> int8_t {
              auto& f = *static_cast<Callback*>(c);
              ++f.calls[2];
              return f.family->getKerning(a, b, static_cast<EpdFontFamily::Style>(s));
            },
            [](void* c, uint32_t a, uint32_t b, uint8_t s) -> uint32_t {
              auto& f = *static_cast<Callback*>(c);
              ++f.calls[3];
              return EpdFont(f.family->getData(static_cast<EpdFontFamily::Style>(s))).getLigature(a, b);
            },
            [](void* c) -> uint8_t {
              auto& f = *static_cast<Callback*>(c);
              ++f.calls[4];
              return f.sd && f.sd->hasAdvanceTable();
            },
            [](void* c, uint8_t s) -> uint8_t {
              auto& f = *static_cast<Callback*>(c);
              ++f.calls[5];
              return f.sd ? f.sd->resolveStyle(s) : s & 3;
            },
            [](void* c, uint32_t cp, uint8_t s) -> uint16_t {
              auto& f = *static_cast<Callback*>(c);
              ++f.calls[6];
              return f.sd ? f.sd->getAdvance(cp, s) : 0;
            },
            [](void* c, uint8_t s) -> int32_t {
              auto& f = *static_cast<Callback*>(c);
              ++f.calls[7];
              return f.family->getData(static_cast<EpdFontFamily::Style>(s))->ascender;
            },
            [](void* c) -> uint8_t {
              auto& f = *static_cast<Callback*>(c);
              ++f.calls[8];
              return f.family->getData()->advanceY;
            },
            [](uint32_t cp) -> uint8_t { return BidiUtils::isTransparentMark(cp); }};
  }
};
inline XpTextmRegistry registry(GfxRenderer& renderer) {
  return {&renderer,
          [](void* c, int32_t id, int32_t* out) -> uint8_t {
            auto& map = static_cast<GfxRenderer*>(c)->fallbackFontMap_;
            auto it = map.find(id);
            if (it == map.end()) return 0;
            *out = it->second;
            return 1;
          },
          [](void* c, int32_t id) -> uint8_t { return static_cast<GfxRenderer*>(c)->fontMap.count(id); },
          [](void* c, int32_t id, uint32_t cp, uint8_t style) -> uint8_t {
            return static_cast<GfxRenderer*>(c)->fontMap.at(id).hasCodepoint(cp,
                                                                             static_cast<EpdFontFamily::Style>(style));
          }};
}
inline std::vector<uint32_t> codepoints(const char* text) {
  std::vector<uint32_t> result;
  while (auto cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text))) result.push_back(cp);
  return result;
}
