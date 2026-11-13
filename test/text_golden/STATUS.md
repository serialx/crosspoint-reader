# Reader status comparisons

`ReaderStatusPixels` compares the shipping Rust composer with unchanged native
status, battery and theme-height methods extracted by `status_methods.py`.
The fixture also extracts the actual native facts callback. The target is the X3 528×792 panel, including the last eight-row strip. Both sides use the
production GfxRenderer/font registry and independent font owners; only the board,
settings, battery and clock facts are controlled. `xp_app_test_reader_status`
exposes the real composer; it does not contain a second drawing implementation.

Each font run covers 2,592 settings combinations in all four orientations and
both battery styles: page-count/percent flags, three title modes, battery and
percentage flags, three clock modes, both time formats, three bar modes and three
thicknesses. Each case compares the scan pass and the subsequent draw. A further
256 cases cover empty/short/long/RTL/Korean titles, strip assembly, RTC absence or
format failure, battery thresholds, ellipsis-only space and nonpositive title
space. Bookmark, estimate, charging, board margins, corner radius and title y
offset vary across cases. These supplementary dimensions are not a complete
Cartesian product. The real title-allocation failure and recovery are checked
once per font run.

CTest runs built-in fonts and three public SD fonts as the primary font. For
qualification, `status_pixels.py BINARY --korean-font PATH` additionally checks a
real Korean SD fallback and asserts that the Korean text resolves to that font.
The optional font stays outside the repository. Each run reports its case and
exact-buffer comparison counts. The tests compare raw buffers, without pixel
tolerances or a rewritten geometry oracle. Native scan-pass rectangle skips and
pixel writes remain observable against a patterned initial framebuffer.

`ReaderStatusLayout` checks percent formatting against host `snprintf`, including
half steps and adjacent floats, signed zero, large values and fixed-buffer
truncation. It also checks cluster positions and the firmware's unsigned 32-bit
progress-bar arithmetic. Host formatting is supporting evidence; firmware linked
and emulator checks still establish the actual target behavior.

The four `reader-status-*` screen groups add parent/candidate captures for book,
chapter, hidden and Korean title settings, bar variants, clock placement and
bookmarks. `capture.py --font-dir DIR` copies optional family folders to each
private card and records their file hashes with the other inputs. The `chapter`
group uses a `StatusHebrew` family and the `cjk` group uses `Pretendard`; provide
those families when qualifying real fallback glyphs. Existing default EPUB
fixture bytes are unchanged. The settings-preview `status-cache` group remains
a native-rendering control.

`ReaderStatusDispatch` compiles the actual shipping facts and paint-dispatch methods
without `app-test`. Controlled state and a paint sink check the single facts read,
direct composer call, title and bookmark/building values. The pixel differential
above covers the actual composer behind that call.
