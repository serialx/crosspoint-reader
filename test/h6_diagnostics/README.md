# Font, XTC and renderer diagnostics

These tests exercise the production adapters and Rust archive. They fail when a
parser drops a failure reason, changes its values or diagnostic order, or when
font lookup and bidi overflow become silent.

```sh
cmake -S test/h6_diagnostics -B build/h6-diagnostics -DCMAKE_BUILD_TYPE=Release
cmake --build build/h6-diagnostics -j 6
ctest --test-dir build/h6-diagnostics --output-on-failure -R H6
```

The SD test reuses the `sdfont_diff` mutation corpus and compares acceptance,
parsed values and exact header-error logs against its frozen C++ loader. It
requires all eight header-error families to occur. Other resident-pool diagnostics
are outside this suite. The XTC test reuses the `xtc_diff` corpus and compares
ordered log lines as well as its existing parser/output assertions. It also
injects failures at each of the first 20 reads and seeks in intact inputs.

By default the frozen references already checked into `sdfont_diff` and `xtc_diff`
are used. To compare the exact upstream source, export `lib/EpdFont/SdCardFont.*`
and `lib/Xtc/Xtc` from `116195b4` outside the repository and pass
`-DH6_REFERENCE=/path/to/export`. The build directory receives only a namespace
rename of the XTC reference and log checks added to the existing test driver.

`H6Gfx` uses the real renderer, font adapters and bidi workspace with the display
stub from `text_golden`. It asserts exact messages on missing fonts, missing
glyphs and the bidi limit, and checks that empty text and scaled missing glyphs
retain their silent behavior. It does not substitute for hardware testing.

Private fixtures can be read in place without adding them to the repository:

```sh
build/h6-diagnostics/H6Xtc build/h6-diagnostics/fixtures /path/to/book.xtc
python3 test/h6_diagnostics/sd.py "$PWD" build/h6-diagnostics --private-font-dir /path/to/fonts
```
