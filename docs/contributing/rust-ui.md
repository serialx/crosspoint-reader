# Rust UI coexistence

The scene runtime in `xp-ui` builds bounded scenes and routes input snapshots. `RustActivityAdapter`
keeps the existing ActivityManager, render task, RenderLock, result objects and
push/pop behavior. A stack can contain both C++ activities and Rust-backed
activities. This runtime does not poll hardware or create tasks.

The wire interface is `rust/crates/xp-ffi/include/xp_ui.h` (ABI version 2).
`FullScreenMessageActivity` is the synchronous entry-only example;
`XtcReaderChapterSelectionActivity` is the stateful list example.

The scene contracts below apply to scene-based activities. The reader uses the
[policy boundary](#reader-policy-boundary) described separately.

## Storage and publication

The owner lends command, text and interaction arrays to a scene builder. The
limits are 256 commands of 32 bytes, 4,096 text bytes, 64 interaction records of
24 bytes and eight nested clip/translation scopes. These limits are ceilings,
not a default allocation. Each screen chooses smaller storage and checks the
builder result before painting. Overflow invalidates the whole build.

XTC allocates its arena once on entry, fallibly, and releases it on exit. On C3
it contains 78 commands, two 32-hit buffers, four borrowed hint pointers and the next
viewport state: 4,088 bytes. Chapter names stay in the immutable Xtc model;
there is no per-chapter UI allocation. A message borrows its owned string and
uses two stack commands, with no new heap allocation.

`view` takes state by value and returns layout feedback separately. The C ABI
leaves state and output descriptors unchanged on failure; scratch arrays may
have changed. The adapter accepts the next viewport and publishes hits only
after successful painting. A short model critical section protects snapshots,
updates and publication. Layout, font services, painting and synchronous panel
refresh run outside that section. RenderLock still protects activity lifetime
and the framebuffer; input never takes it. A failed build closes touch routing
and leaves the previous frame displayed. Navigation effects run after unlocking.

XTC alternates its hit buffers: the displayed table stays immutable while the
next table is built. Rust commits the displayed viewport without replacing current selection,
held-repeat or touch state. A navigation revision distinguishes scroll requests
included in the rendered snapshot from scroll requests after a new selection;
only consumed requests are removed. Input arriving during layout remains pending
for the next render. No snapshot queue or per-frame
allocation is needed; each interested input frame is applied immediately.

Scene descriptors are borrowed views, not owning containers. Their arrays and
resource tables must remain live and immutable across validation, painting and
any subsequent strip passes. The published interaction table must stay live
until replacement or screen exit. Reusing a scratch array requires the owner's
render lock; a frozen overlay parent requires separate retained storage.

## Drawing and resources

Geometry uses the `xp_core::strip::Strip` packed DrawTarget, including orientation, clipping and
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

`xp_ui_render_layers` accepts up to four `XpUiLayer` bindings in painter order.
Each binds a frozen scene to its own borrowed `XpUiPaint` services/context, so
identical local handles may name different resources in parent and popup. It
validates every binding before painting any layer. Rust's `draw::Layer` has the
same ownership contract. All layers share the supplied pixel target.

Clear, text and resource callbacks receive that target explicitly. The shared
GfxRenderer scene service binds the target for the entire call, so existing font
and resource painters write the same physical band as geometry. The supplied
target must match the renderer's dimensions, orientation and render mode;
its band storage, origin and clip may differ. The previous target/clip is restored
on return. Resources must use this bound renderer or the supplied target rather
than a separately captured framebuffer. `xp_ui_hit` searches hits in
reverse painter order and rejects stale generations. Inherited-release
suppression and global gesture ownership remain ActivityManager responsibilities.

## Input dispatch

`XpUiInput` carries named bits for all 15 mapped buttons (press, release and held),
touch press/release/held coordinates, all four swipe directions and wrapping
millisecond timestamps. It reads the existing frame, never polls hardware again.

Each Rust screen exposes `XpUiInterest`; the adapter consults it under the short
model lock, without RenderLock. Press/release masks, touch flags and swipe bits
select immediate work. A held mask can require a minimum held duration and a
repeat deadline; with an empty held mask, an enabled deadline requests a timer
tick without input. Deadlines must be within half the unsigned timer range.
Rust supplies the interest and owns repeat/hold policy. The adapter only samples,
serializes updates and publishes completed layout. XTC requests navigation
press/release, Back/Confirm release, touch edges, vertical swipes, and held
navigation after 500 ms at the existing repeat cadence. The static message
screen requests no input or timer work.

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
also require the normal host/build checks and [UI screen oracle captures](../../test/ui_screens/README.md) for `sd-error` and
`xtc` on both boards and polarities. The standard emulator workloads check
unrelated reading behavior. Device verification remains part of the human
handoff.

## Reader policy boundary

Reader policy lives in `rust/crates/xp-ui/src/reader/`. Thin FFI exports in
`rust/crates/xp-ffi/src/ui_reader.rs` and the contract in
`rust/crates/xp-ffi/include/xp_ui_reader.h` connect it to native service contexts
in `src/activities/reader/ReaderActivity.cpp` and `EpubReaderActivity.cpp`.
Native EPUB scalar state remains authoritative; a canonical Rust EPUB model is
deferred. The shared ReaderActivity model and session are Rust-defined.

Services borrow their context and resources synchronously. Retained child-result
callbacks capture the native owner and owned values, then create a fresh service
context when called; they never retain a stack service descriptor. Rust guards
pair BEGIN/END operations to release native resources on every return path.
ModelLock permits only bounded scalar and POD copies. Allocation, reference-count
changes, storage, logging and blocking locks stay outside that critical section.
The port preserves the existing RenderLock scopes, including reader input paths
that already needed them; the scene input rule above does not redefine those scopes.

The [reader lifecycle oracle](../../test/reader_lifecycle/README.md) documents
host parity checks and their limits. These checks do not establish hardware
qualification.
