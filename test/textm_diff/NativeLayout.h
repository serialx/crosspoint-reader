#pragma once

// Same workload as LayoutCalls.h, using production table descriptors.
inline void nativeLayout(Test& test, const XpNativeProvider& provider) {
  struct Context {
    const XpNativeProvider* provider;
    unsigned lines;
  } context{&provider, 0};
  XpParaMetrics metrics{};
  metrics.context = &context;
  metrics.native = [](void* c, XpNativeProvider* out) -> uint8_t {
    *out = *static_cast<Context*>(c)->provider;
    return 1;
  };
  metrics.prepare = [](void*, int32_t, const XpParaSpan*, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t) {};
  const XpParaAllocator allocator{std::malloc, std::free};
  constexpr const char* words[] = {"office",      "AVATAR",     "discretionary", "internationalization",
                                   "measurement", "rendering",  "reader",        "a",
                                   "font",        "hyphenation"};
  for (uint8_t focus : {0, 1})
    for (uint8_t hyphen : {0, 1})
      for (uint16_t width : {80, 240}) {
        test.setups = 0;
        test.misses = 0;
        context.lines = 0;
        for (int paragraph = 0; paragraph < 100; ++paragraph) {
          alignas(uintptr_t) uintptr_t storage[64]{};
          auto* state = reinterpret_cast<XpParagraph*>(storage);
          XpParaBlock block{};
          block.font_scale = 1.0f;
          xp_para_init(state, &allocator, &block, hyphen, focus, 0, [](XpParaSpan, int32_t) -> uint8_t { return 0; });
          uint32_t offset = 0;
          for (int i = 0; i < 20; ++i) {
            const char* word = words[(paragraph + i) % std::size(words)];
            auto size = static_cast<uint32_t>(strlen(word));
            xp_para_add_word(state, {reinterpret_cast<const uint8_t*>(word), size}, 0, 0, 0, offset, 0, 0, 0);
            offset += size + 1;
          }
          XpParaLayout options{
              1, width, 1, 0, 100, UINT32_MAX, 0, nullptr, &context, [](void* c, const XpParaLine*) -> uint8_t {
                ++static_cast<Context*>(c)->lines;
                return 1;
              }};
          xp_para_layout(state, &metrics, &options);
          require(!xp_para_dropped(state), "native layout OOM");
          xp_para_destroy(state);
        }
        require(test.misses == 0, "native layout called glyph miss");
        require(test.setups == 100, "native layout repeated descriptor setup");
        std::printf(
            "words=2000 focus=%u hyphen=%u width=%u advance=0 advance/word=0 space=0 kern=0 space_advance=0 ascender=0 "
            "prepare=0 lines=%u setups=%u glyph_callbacks=%u\n",
            focus, hyphen, width, context.lines, test.setups, test.misses);
      }
}
