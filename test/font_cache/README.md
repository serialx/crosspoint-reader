# Font lifecycle oracles

These frozen transcripts were recorded from the C++ implementation before the
font lifecycle moved to Rust. Do not regenerate them from the candidate when a
comparison fails.

- `FontCacheGolden` uses small synthetic compressed fonts with contiguous and
  mapped groups. It checks bitmap bytes, ligature prewarm, slot limits, cache
  statistics, generation changes, diagnostic text, allocation/free order and
  failures at the first twelve allocation positions. It also injects corrupt
  compressed data.
- `FontManagerGolden` uses the real manager and registry with renderer/font
  leaves that record operations. It checks replacement, exact-size reuse,
  independent variants, collisions, failed creation/loading, unload order and
  family-name identity, including embedded null bytes.
- `VectorFontGolden` uses the SDK FreeType implementation and the repository's
  Noto Sans font. It checks all four styles, glyph bytes and metrics, kerning,
  cache clearing, resident release, reload and prewarm. Heap limits cover normal
  growth, denied growth, bitmap and kerning pressure and eviction recovery. Allocation
  order is hashed into the transcript. FreeType's retained global library is
  counted separately from face/cache storage and must not grow between cases.

Run them from the repository root:

```sh
cmake -S test -B build/test
cmake --build build/test --target FontCacheOracle FontManagerOracle VectorFontOracle
ctest --test-dir build/test --output-on-failure \
  -R '^(FontCacheGolden|FontManagerGolden|VectorFontGolden)$'
```

The cache and vector tests record allocator lifetimes. They do not substitute
for device heap measurements or cover every FreeType font format. Existing
`sdfont_pool_diff`, `sd_font_registry`, `text_golden`, `page_golden` and
`glyph_diff` tests cover the surrounding font and rendering contracts.
