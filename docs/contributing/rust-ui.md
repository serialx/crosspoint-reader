# Rust UI coexistence

`xp-ui` builds bounded scenes and routes input snapshots. `RustActivityAdapter`
keeps the existing ActivityManager, render task, RenderLock, result objects and
push/pop behavior. A stack can contain both C++ activities and Rust-backed
activities. This runtime does not poll hardware or create tasks.

The wire interface is `rust/crates/xp-ffi/include/xp_ui.h` (ABI version 1).
`FullScreenMessageActivity` is the synchronous entry-only example;
`XtcReaderChapterSelectionActivity` is the stateful list example.

## Storage and publication

The owner lends command, text and interaction arrays to a scene builder. The
limits are 256 commands of 32 bytes, 4,096 text bytes, 64 interaction records of
24 bytes and eight nested clip/translation scopes. These limits are ceilings,
not a default allocation. Each screen chooses smaller storage and checks the
builder result before painting. Overflow invalidates the whole build.

XTC allocates its arena once on entry, fallibly, and releases it on exit. On C3
it contains 80 commands, 32 hits, four borrowed hint pointers and the next
viewport state: 3,380 bytes. Chapter names stay in the immutable Xtc model;
there is no per-chapter UI allocation. A message borrows its owned string and
uses two stack commands, with no new heap allocation.

`view` takes state by value and returns layout feedback separately. The C ABI
leaves state and output descriptors unchanged on failure; scratch arrays may
have changed. The adapter accepts the next viewport and publishes hits only
after successful painting, while holding RenderLock. Input reads that published
table under the same lock. A failed build closes routing and leaves the previous
frame displayed. Effects that pop activities run after releasing the lock.

Scene descriptors are borrowed views, not owning containers. Their arrays and
resource tables must remain live and immutable across validation, painting and
any subsequent strip passes. The published interaction table must stay live
until replacement or screen exit. Reusing a scratch array requires the owner's
render lock; a frozen overlay parent requires separate retained storage.

## Drawing and resources

Geometry uses the R1 packed DrawTarget, including orientation, clipping and
strip origins. The current adapter lends the existing full framebuffer as one
full-height strip. It does not allocate another framebuffer or borrow its bytes
as arena storage.

Paint values are black, white, light dither and dark dither (0 through 3).
Rounded-corner masks use top-left 1, top-right 2, bottom-left 4, bottom-right 8;
zero means all four. Omitted rounded stroke corners follow the existing
GfxRenderer behavior, including omitted edge segments. Line commands store an
endpoint delta in `rect.w/h`; stroke width applies to rectangle strokes.

Text uses the existing font IDs, metrics, bidi, fallback and glyph services.
`max_lines == 0` paints at the recorded glyph origin; positive values use the
existing SDK text-box service. XTC hint labels also reuse its existing font
layout helper. These services remain C++ facades over the existing Rust text
code; there is no second font or line-breaking engine.

Resource commands hold a handle and generation. The owner validates every
resource before the first pixel changes, then paints it synchronously. A
resource may represent immutable text, a bitmap/icon or an existing page arena;
page contents are not expanded into per-glyph scene commands. Callbacks cannot
retain borrowed pointers, reenter the UI runtime, or change scene/target
metadata. Paint callbacks may write framebuffer pixels: the Rust mutable
framebuffer loan has ended before they run. Resource validity must remain
stable throughout the call.

`xp_ui_render_layers` accepts up to four frozen scenes, in painter order. It
validates every layer before painting any layer. `xp_ui_hit` searches hits in
reverse painter order and rejects stale generations. Inherited-release
suppression and global gesture ownership remain ActivityManager responsibilities.

## Invalidation and verification

An update returns an effect and a repaint flag. These first screens conservatively
rebuild the whole scene. `XpUiDamage` accepts native-coordinate rectangles; a
ninth rectangle switches to full-screen damage. Raster damage does not change
waveform policy. Messages retain their caller-selected refresh mode and XTC
requests FAST, including the existing renderer's one-shot promotion behavior.

Run `cargo test -p xp-ui` from `rust` and the `ui_runtime_*` CTest cases. The tests
compare frozen SDK list geometry, glyph origins, C++ integer widget pixels and
message placement; they also cover strip equivalence, input transitions,
resource validation, overlay failure and C/Rust ABI layout. Firmware changes
also require the normal host/build checks and O2 captures for `sd-error` and
`xtc` on both boards and polarities. The standard emulator workloads check
unrelated reading behavior. Device verification remains part of the human
handoff.
