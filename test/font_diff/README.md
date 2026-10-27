# Font table oracle

`FontDiff` runs the production EpdFont and SdCardFont implementations on the host.
It visits every built-in interval codepoint, interval edges and missing codepoints,
every left/right kerning-table pair, ligature pairs and chained substitutions,
and solid-block fallback metrics. Hashes encode fields explicitly, excluding padding.
`builtin-oracle.txt` was captured before the Rust font port on c9330408.

```sh
cmake -S test/font_diff -B build/font-diff
cmake --build build/font-diff -j 6
build/font-diff/FontDiff
build/font-diff/FontDiff /path/to/public.cpfont /path/to/private.cpfont
```

SD files are supplied explicitly and never copied into this directory. The existing
storage stub lends their bytes to the real loader. The test prepares advances using
`buildAdvanceTable`, the operation called by `ensureSdCardFontReady`, then prewarms
resident glyph/kern tables, clears the cache, and releases resident caches.
Test-only private-member inspection exposes resident advance and coverage tables;
production code and cache ownership are unchanged.
