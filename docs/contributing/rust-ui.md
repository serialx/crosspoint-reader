# Rust UI coexistence

The Rust application in `xp-ffi/src/app` owns the main-loop policy, current activity,
retained parents, pending transition, Home gestures and screen, and EPUB session. `ActivityManager`
forwards native callers through `xp_app.h`; it retains the physical render task,
RenderLock and completion notification. There is one activity stack, owned by
`xp-ui::app::Application`. Its entries are Rust Home, EPUB session and reader child
owners, or owned legacy native activities such as XTC and the retained native
children. A legacy pointer transfers only when the Rust event accepts it.

`xp_app_init`, `xp_app_loop`, `xp_app_render` and `xp_app_event` are the application
boundary. Typed legacy, resource and runtime tables supply existing services;
`xp_epub_session.h` supplies the physical leaves for the EPUB session. All four
service tables, including the complete EPUB table, are required by `xp_app_init`.
Legacy activities retain their individual models and resources until their owners
move. General button remapping stays in `MappedInputManager`; Rust owns Home timing,
deferred actions, global navigation and loop dispatch.

The scene interface is `rust/crates/xp-ffi/include/xp_ui.h` (ABI version 2).
`FullScreenMessageActivity` is the synchronous entry-only example;
`XtcReaderChapterSelectionActivity` is the stateful list example.
`SettingsUiActivity`, `UpdateUiActivity` and `CatalogUiActivity` share bindings
within their families. Library, file browser and network screens use the
`RustActivityAdapter` with their Rust models and native resource bindings.
Home calls its Rust model and view directly and has no native activity class.

## Application ownership

Replace and sleep exit and drop the current owner and retained parents under
RenderLock; push retains the parent without exit. On pop, the application takes
the result before exiting and destroying the child under RenderLock. It restores
and publishes the parent as the render target, then unlocks before synchronous
result delivery. Destruction of any Pages transferred to the child precedes
parent delivery.

Rust reader children return an owned `ReaderChildResult` through
`ActivityResult::Child`. The EPUB parent's typed receiver consumes it through
its pending child intent; a selected chapter anchor moves into navigation state.
For retained native children, the native bridge instead holds the result in
callback-local storage and lends a typed view through `ActivityResult::Legacy`.
That storage lives until delivery returns. A native-result handler must copy
borrowed text only if it retains the text beyond that call. Native parents keep
their native forwarding path. Handlers enqueue transitions; the outer driver
drains them after the result producer returns, so a callback cannot destroy the
storage it is still reading. Entry also runs after unlocking.

Loop-only navigation and driver state are separate from render-only command/text
storage. Brief model critical sections protect scalar snapshots and publication.
They contain no allocation, storage, logging or blocking calls. RenderLock keeps
activity pointers alive through rendering and destruction. Home entry publishes
readiness only after its identities are complete, so an early render notification
defers rather than borrowing partly initialized data.

Home owns recent identities, cover paths, typed text/resources, prepared wrapped
lines, commands and the cover cache. Commands use a reused fallible vector bounded
at 128 entries; wrapped title buffers are reused during preparation. Clipped
author and menu labels copy once per scene build. Drawing borrows the prepared
strings. Numeric resource encoding
exists only at the shared scene boundary. The native `RustAppResources` leaves
supply storage/book/font/display services; they do not own Home selection or
publication. The grid cache retains the capability-aware PSRAM allocation path.

Home's display keys include Continue and distinguish Plugins from OPDS, while the
navigation enum describes destinations. `HomeState::menu_keys` supplies the display
order; `home_text.rs` maps those keys to translations, and `PaintContext` maps them
to native theme icons. These mappings remain separate because translations and
icons are native services; a row change must update all three together.

`STR_CONTINUE_READING`, `STR_BROWSE_FILES`, `STR_NO_OPEN_BOOK`, `STR_START_READING`
and `STR_RESUME` are referenced only under `rust/crates`. The default scan includes
it; when generating with `--strip-unused` and an explicit `--src-dirs`, keep
`rust/crates` alongside `src` and `lib`.
`test/i18n_oracle/test_ids.py` checks the mixed-language scan and matching IDs.

The former Home exports and mirrored C layouts are removed. `xp_ui_home.h` still
serves FileBrowser and Library, including their shared scene-status enum. The
`app-test` feature provides host-only setup, observations and historical oracle
adapters; it must never be enabled for firmware. Some adapters replace named inner
methods to preserve their original fixture boundary. Separate composition tests
exercise the ordinary owner through App without those policy replacements.
Firmware symbol inventories must exclude both `xp_app_test_*` and `xp_epub_test_*`.
The reader oracle documentation describes the scope of each kind of test.

## Reader child ownership

| Route | State, storage and results | Native services |
| --- | --- | --- |
| Menu, Chapters, Percent and QR | [`epub/children/`](../../rust/crates/xp-ffi/src/app/epub/children/) owns each child; Application retains its EPUB parent and delivers owned results | [`RustReaderDrawing.cpp`](../../src/activities/reader/RustReaderDrawing.cpp) supplies theme/layout, header/footer and font services; the QR leaf in `RustEpubResources.cpp` calls `QrUtils::drawQrBytes` |
| Toolbar | `children/toolbar_owner.rs` and `children/toolbar_view.rs` own the session's toolbar slot, row window and publication | The same drawing leaves supply metrics, icons and painting |
| TextSettings, Bookmarks, Clippings, Wi-Fi, sync, footnote, dictionary and selection | Retained native children use the legacy activity/result boundary | [`RustEpubResources.cpp`](../../src/activities/reader/RustEpubResources.cpp) retains their factories and services |

`ReaderChromeListActivity` remains for `EpubReaderBookmarksActivity` and
`EpubReaderClippingListActivity`. FileBrowser, Library and TextSettings instead
use `RustActivityAdapter`; they still use `ReaderChromeCommands` and the theme
sampler. `UtilityActivity` remains for `ConfirmationActivity`, `DetailsActivity`,
`IntervalSelectionActivity` and `FrontlightPanelActivity`. Removing the reader
QR and Percent classes does not retire either shared base or the sampler.

The scene contracts below apply to scene-based activities. The reader uses the
[policy boundary](#reader-policy-boundary) described separately.

## Storage and publication

The owner lends command, text and interaction arrays to a scene builder. The
limits are 256 commands of 32 bytes, 4,096 text bytes, 64 interaction records of
24 bytes and eight nested clip/translation scopes. These limits are ceilings,
not a default allocation. Each screen chooses smaller storage and checks the
builder result before painting. Strict arena overflow invalidates the whole build.
The reader Percent picker uses a separate narrow interval entry with 18 inline
commands: it reports dropped drawing commands while still publishing the full
interaction table. Its exhaustive host bound covers all normal drawing states.

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
and the framebuffer; input never takes it. A failed build or paint rejects activation, clears pending gestures and leaves
the previous frame displayed. Back remains available for adapter screens; Home preserves its existing suppressed-Back behavior. A successful publication
restores routing after held-over input has been released. Rejection arms suppression
for all buttons and touch. The first loop keeps only held inputs suppressed through
their release. A fresh gesture arriving between failure and that first loop may be
consumed, even if rendering has already recovered. An empty frame clears suppression;
after a consumed gesture is released, the next gesture works. Navigation effects run after unlocking.

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

### Shared scene and list policy

`xp_ui::runtime::SceneGate` stores four bytes in each participating owner. Native
`RustActivityAdapter` calls the checked `xp_ui_scene_gate_*` bridge; owned Menu and
Chapters call the typed methods. Rejection suppresses activation, including when
painting recovers before the next input frame. Current publication clears failure
but keeps held controls suppressed through release. Stale publication requests
another paint without marking a successful frame as failed. Callers retain their
own Back and custom cancellation behavior, model locks and resource lifetimes.

Mutate the gate under the caller's model lock; the gate does not synchronize
access itself. If an action waits for a render, resource or text lock after
admission, recheck admission after that wait, before acting. A render may reject
the scene during the wait. Native callers use `rustSceneRejected()`; owned
Chapters evaluates `ListPublication::input` under its model lock after resource
access, so its gate sees any intervening rejection.

`runtime::next_capacity` computes the next list command capacity; it does not
allocate. Native `ReaderChromeCommands` and owned `children::Commands` release
the old allocation before growing and retain the requested capacity even if
allocation fails. A later reserve retries that request. FileBrowser starts at 32
and grows by 4; Library starts at 48 and TextSettings at 32, both growing by 16.
ReaderChrome chooses its initial capacity by screen, including 40 for Bookmarks.
These list buffers stop at 128 commands. Settings keeps its separate initial
8/16/24/32/48 choices and unclamped +16 retry loop: starts at 8 or 24 reach 120
and reject 136, without a 128-command attempt. Keyboard
retains its independent doubling buffers, allocates replacements before releasing
the old buffers, and keeps the scene's 8 KiB command ceiling. Keyboard and utility
dialogs do not use reader-list preparation.

`reader_chrome::runtime::ListRequest::prepare` borrows a model snapshot, theme,
resources and owner scratch. It applies the existing nine-pass follow rule and
returns `PreparedList` with the finished command count and refresh choice,
without painting or publishing. The checked FFI writes both into its scene;
owned callers draw without refreshing, publish, then use that refresh choice.
A capacity retry begins
from the same snapshot. Callers paint only a completed scene, validate the
current revision and publish through their model; pending scroll is consumed
only once. Resource descriptor, window and generation checks remain required.
No shared dispatcher owns per-screen command, hit or theme scratch.

`sampleReaderChildTheme` in `RustReaderDrawing.cpp` samples native geometry and
current font facts. `ThemeFacts::compose` applies bookmark help-font, touch row-height
and spacing choices in Rust; the retained native sampler calls
`xp_ui_reader_theme_compose`. Toolbar keeps its own safe rectangle and row
minimum. The 12-byte facts value is local preparation storage, with no settings
cache. `UITheme`, its SDK storage and physical header/footer/font services remain
in use. Bookmarks and Clippings still retain their native row-resource codecs
until their ownership ports retire those consumers; owned Menu and Chapters use
the typed `RowSlot` codec. Physical refresh timing and resource-release order stay at each
caller, including UpdateUi's release-text-before-publication sequence.

The wire bridges are temporary Phase 18 compatibility seams:
`xp_ui_scene_gate_*`, `xp_ui_command_next_capacity` and
`xp_ui_reader_theme_compose` retire only when their last actual native callers
retire. Rust owners already call the typed functions directly. Keep the common
commands and sampler in `ReaderChromeListActivity.{cpp,h}` while FileBrowser,
Library, TextSettings or EndOfBookOptions still use them; retiring native
Bookmarks and Clippings alone does not remove those consumers. Keep reader/XTC
exports and physical drawing services for as long as their callers remain.

## Drawing and resources

Geometry uses the `xp_core::strip::Strip` packed DrawTarget, including orientation, clipping and
strip origins. The current adapter lends the existing full framebuffer as one
full-height strip. It does not allocate another framebuffer or borrow its bytes
as arena storage.

Paint values are black, white, light dither and dark dither (0 through 3).
Rounded-corner masks use top-left 1, top-right 2, bottom-left 4, bottom-right 8;
zero means all four. Omitted rounded stroke corners follow the existing
GfxRenderer behavior, including omitted edge segments. Line commands store an
endpoint delta in `rect.w/h`. Line width repeats the path at successive y offsets,
matching the native renderer; zero retains the one-pixel default. Rectangle
strokes use their own inward stroke width.

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
suppression remains with the relevant screen model; global gesture ownership belongs to the Rust app.

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

An update returns an effect and a repaint flag. Screens conservatively rebuild
the whole scene; damage rectangles are not exposed across the ABI. Messages retain their caller-selected refresh mode and XTC
requests FAST, including the existing renderer's one-shot promotion behavior.

Run `cargo test -p xp-ui` from `rust` and the `ui_runtime_*` CTest cases. The tests
compare frozen SDK list geometry, glyph origins, C++ integer widget pixels and
message placement; they also cover strip equivalence, input transitions,
resource validation, overlay failure and C/Rust ABI layout. Firmware changes
also require the normal host/build checks and [UI screen oracle captures](../../test/ui_screens/README.md) for every affected family on both boards and polarities.
The message and chapter-list examples use the `sd-error` and `xtc` groups. The standard emulator workloads check
unrelated reading behavior. Device verification remains part of the human
handoff.

## Reader policy boundary

`EpubSession` in `rust/crates/xp-ffi/src/app/epub/` owns the EPUB session's scalar
state, lifecycle, input decisions, page movement, menus, child-result intents and
render sequencing. Its modules call ordinary Rust methods directly. The shared
helpers in `rust/crates/xp-ui/src/reader/` remain available to EPUB and XTC where
needed; the retired EPUB-exclusive service dispatch is not the session boundary.

`src/activities/reader/RustEpubResources.cpp` implements the typed physical leaves
in `rust/crates/xp-ffi/include/xp_epub_session.h`. The session owns its loaded
`ReaderBook` and `ReaderSection` in Rust, including metadata, cache, load and
incremental Chapter build resources. The native `Epub` facade lends the same
book to retained native child and sync consumers. Rust's `page_owner` owns reader
Pages and Page metadata. C++ retains dictionary/preview Pages, borrowed/transferred
Page facades for the three native Page consumers, the remaining native child
screens and physical leaves, including file implementations, stores and
font/display services. Rust owns Menu, Chapters, Percent, QR and Toolbar; native
drawing leaves supply their theme metrics, font preparation and painting. Rust
owns the inline render frame, prewarm storage, plane/scratch buffers
and optional strip storage, including source/plan loans and cleanup. Physical
leaves perform native font, paint and display operations. In `XpEpubPhysicalQuery`,
`WIDTH` is the panel width in pixels; `WIDTH_BYTES` is the row stride in bytes
(equal to width / 8 on current panels). See
[Reader Page ownership](rust.md#reader-page-ownership)
for Page lifetimes and [Reader book ownership](rust.md#reader-book-ownership)
for facade loans, cache locking and teardown. `ReaderActivity` and
`XtcReaderActivity` remain the native XTC owner path with shared reader helpers.
Reader launches for XTC/XTCH use that native owner; EPUB, TXT, Markdown and the
existing non-image fallback use `EpubSession`. Empty paths open Files, and BMP,
PNG, JPG and JPEG paths retain their bitmap route. Extension matching remains
case-insensitive.
The retained native resource bridge is a real lifetime and allocation boundary;
moving session policy does not move all resource storage into Rust.

ModelLock protects bounded scalar state reads and publication. It permits no
allocation, reference-count changes, storage, logging, blocking locks or physical
callbacks. Model borrows end before calling a physical leaf. `ResourceAccess`
represents access under the existing nonrecursive RenderLock. A render borrows
its caller's held lock; a loop operation acquires and releases a token at its
resource access boundary. Helpers receive that access rather than reacquiring the
lock. They recheck live Section/Page resources after acquisition. Published scalar
facts can be read without touching those live owners; live text, offsets
and Page operations require resource access. The EPUB page cursor is Rust state.

Render entry declines work until the owner is ready. A render uses a scoped frame
and publishes its scalar results without overwriting input queued during that
frame. No model or mutable frame borrow may survive a callback. Pending owner
discard releases only its acquired components; an entered owner's exit performs
base exit and book teardown before component destruction. It must not clear an
active owner's shared stores when discarding an unentered replacement.

Borrowed text is consumed synchronously at its leaf or result boundary. Rust-owned
Chapters and Toolbar read TOC rows under resource access and copy labels into
their bounded owner-held windows. Drawing borrows those windows for the current
paint; it does not retain the book's borrowed row source. Retained native
consumers may still borrow the current chapter title or facade row data. Native
children can outlive a launch call, so their retained values must be owned.
Their returned text follows the callback-local lifetime described above.

The scene input rule above does not redefine reader resource-lock scopes. The
[reader lifecycle oracle](../../test/reader_lifecycle/README.md) distinguishes
frozen historical traces, ordinary-method tests and App composition tests. Those
host checks do not establish target stack, heap, input timing or hardware
qualification.
