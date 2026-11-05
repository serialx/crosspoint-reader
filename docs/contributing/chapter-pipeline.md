# Chapter pipeline boundary

Rust owns EPUB parsing and layout, page records, page-byte construction and
load-time decoding and page rendering through `xp-core::page`. The reader owns
its Page allocation, metadata and render resources in Rust; render frames borrow
the arena and native selection children adopt it through a facade. C++ retains
file I/O, font registration/preparation, built-in/TTF caches, JPEGDEC integration
and physical display operations. Dictionary and settings-preview Pages remain
native.
Glyph bitmap views and rasterization run in Rust. The Rust reader also owns its
loaded book, Section and incremental Chapter build. A native `Epub` facade forwards
child and sync queries to that same book; the dictionary retains its native
Chapter adapter. See [Reader book ownership](rust.md#reader-book-ownership) and
[Section ownership](rust.md#chapter-and-section-compatibility), and
[Reader Page ownership](rust.md#reader-page-ownership).
The Rust application owns navigation, Home and the ordinary EPUB `EpubSession`; native XTC
and reader child screens remain owned legacy activities. The session calls typed
physical leaves for those resources without changing the chapter assembly and
page-storage boundary described here. The wider reader also uses Rust for
[shared inflate](rust.md#zip-directory-boundary),
[SD font parsing/cache policy and XTC parsing](rust.md#sd-font-and-xtc-parsers),
and [PNG/GIF decoding](rust.md#image-pixel-pipeline).

## Rust chapter assembly

`chapter::assembly` owns the current paragraph, ordered page elements and
positions, box scopes, six carried-line offsets, orphan/widow and keep-with-next
state, buffered table paragraphs and column lines, pending footnotes,
visible offsets, the image counter and popup latch, and stop/OOM latches. Semantics and assembly share one fallibly allocated
chapter handle. Its production paragraph owner calls `para::paragraph::Paragraph`
and the layout engine directly. The paragraph trait also permits focused mock
tests for carry, placement and allocation failure.

The Chapter selects a typed reader backend or the retained native dictionary
sink. Both are synchronous; the reader backend calls Rust build services directly.
The operation contracts are:

| Sink operation | Contract |
| --- | --- |
| Process image | Rust selects cache suffixes/counters, probe/extraction retries and failed-cache removal. The reader owns path/input/scratch lifetime in `section_owner`; native decoder, file and framebuffer leaves perform physical operations. `ChapterPlatform.cpp` supplies the retained dictionary backend. Resolved paths remain in the selected backend until the next probe. Rust computes CSS sizing and placement in the original order. |
| Reclaim paragraph memory | Preserve the cache-reclamation retry on paragraph allocation failure; request 4 KiB of free memory, then retry once. |
| Font provider and preparation | C++ resolves scale to font ID, prepares fonts and supplies native descriptors and dynamic handlers. Rust reads line metrics and computes advances, spaces, kerning, codepoint metrics and SD-font status. Host callback adapters retain the metric query slots. |
| Complete page | Rust queries whether a byte sink is installed, then selects it or the native Page decoder. The selected sink borrows one complete encoded page, with u16 paragraph/list indices and u32 visible offset. The reader performs one physical file write and records the same LUT entry in its Rust build records. The callback reports success, stop or failure; it retains no byte pointer. |
| Popup | Show the existing indexing popup at the original probe/extraction point. The image single-shot latch is Rust-owned and distinct from the driver's large-file popup. |
| Anchors | The reader owns pending/TOC IDs and completed anchors in Rust, moving text into fixed-size record chunks. The dictionary backend retains its native strings. Both preserve membership decisions and the original u16 page transition. |
| Table prescan | The reader lends its scoped Rust input adapter; the dictionary lends a local `XpChapterReader`. Rust owns open/seek/measure/plan sequencing and exclusively reborrows the active state. The file closes when the visitor returns. |

Rust consumes each packed `XpParaLine` synchronously and encodes its text, style,
font slots and ruby through the page builder. Metrics use native font descriptors;
synthetic host renderers retain scalar metric callbacks. The narrow `font_ready`
callback preserves `ensureFontLoaded` and fallback before measuring inline font
ascent; it creates no page object. Image probing retains paths in the selected
backend until the image encoder has consumed their borrowed spans. No `ImageBlock` is constructed
on the Section build path.

Rules, drop caps, boxes and table separators remain plain records until completion.
Encoded lines and images live in one chapter-owned byte buffer. Slot indexes survive
compaction and table/carry reordering; payload destruction releases the slot. A
separate typed buffer retains layout links until placement copies them into page
links. Completion compacts live payloads, moves emitted records into page order in
place, patches positions, and appends the existing footnote/link trailer. It then
lends the prefix to the selected sink and removes it after return, preserving carried and buffered
table payloads. There is no second page output buffer. Pending-anchor spans last
until defer/record.
Stored page links own their bytes. Reader anchors live in `BuildServices`, outside
the Chapter handle, so Section can commit them after Chapter finalization. Text
moves into the completed record; no new 256-byte truncation is introduced.
The retained dictionary facade keeps its native anchor store and `getAnchors`
interface. TOC membership borrows the selected backend's entries; paragraph and
assembly operations remain direct Rust calls. Widths
remain those listed below: u8 flags/kinds, i16 coordinates,
i32 font metrics/IDs, f32 scales/compression, and u32 counts. A record never
transfers a Rust allocation to C++. A callback may set the separate C++ stop latch but cannot feed,
destroy, or recursively lay out the handle. The completion sink returns the latch
after `completePageFn` returns; Rust assembly then latches the result, and semantics
applies its feedback after the assembly operation returns. Completed-page elements are consumed even when
the callback requests stop; carried elements remain Rust-owned until transferred
or dropped.

Rust preserves eight box scopes, six carried lines, four table columns and the
160-token/2,048-byte row fallback. Dynamic buffers use `try_reserve`; their owner
is the chapter and their lifetime ends on completion/abort. Box storage grows
only when a box is encountered, up to eight scopes. Encoded bytes, slot metadata
and typed line-link capacity are retained for the build. Growth uses checked
`try_reserve_exact` at the requested size, without geometric spare capacity.
The internal allocator can extend alignments up to four bytes with
`heap_caps_realloc`; larger alignments retain allocate/copy/free. Failed growth
preserves the original buffer. A moved allocation can still overlap old and new
storage inside the allocator; a larger page still needs a larger contiguous block. Capacity is bounded by checked addressable
lengths and existing page/line count limits, not by a new content-size limit.
There is no unconditional allocation at a page boundary. Table line/offset scratch is
local to the synchronous row operation and is freed on every exit. No second paragraph text copy or full-framebuffer allocation is added.
The evidence report records C3 sizes, transient growth peaks and allocation
counts; the mock tests alone do not establish those resource costs.

The existing 312 section streams and 5,529 layout calls remain the output
oracles. `chapter-trace` emits the old operation names, replies, field order
and f32 bits at the corresponding direct Rust calls. The explicit exclusion set
in `run.py` lists the migrated paragraph and scalar assembly queries; unknown
operation names still fail comparison. Nested font metric records and replies
remain observable as semantic observations, not production FFI-call counts.
Behavioral block/page/table operations retain their trace. Page records, image
dimension/sizing observations, block/box
changes, table results, anchors and stop feedback remain observable. The frozen
C++ instrumented executable is retained before porting; no extra element oracle
is needed because the section read oracle already exposes each element and its
coordinates. Private operation streams remain external evidence. Integrated chapter tests cover
multibyte drop caps, footnote labels carried across a page break, and Korean/RTL
image fallback text. Paragraph-level tests separately cover shaping, bidi, CJK
line breaking and source ranges; they do not by themselves prove assembly parity.

Lifecycle and cancellation are specified in Ownership and lifetime below. Image
sizing retains its distinct round-to-nearest versus truncation operations; carry
logic must never move every element off a page.

The chapter parser consumes XML events and CSS rules, then builds paragraphs
and pages. The Rust semantic layer preserves the order of its direct assembly operations,
including results that influence later parsing. The chapter operation oracle in `test/section_golden` records the
C++ implementation captured before the port.

## Ownership and lifetime

Semantics owns element depth, hidden-content tracking, CSS ancestors and style
stacks, word fragments, inline continuation, lists, ruby collection, link-label
collection, and visible-text/XPath counters. Rust assembly owns paragraph storage,
layout, page records, carry, box and table state. The reader's `BuildOwner` owns
the Chapter and stable `BuildServices`: input/files, CSS, paths, image scratch,
framebuffer loans, pending/TOC/completed anchors and Page/LUT records. It drops
Chapter before services, and holds no mutable owner borrow across a physical
callback. Native leaves retain hardware access, font preparation and image I/O.
Section consumes encoded bytes directly. Styled dictionary definitions and
historical host tests retain the native sink and owned-`Page` callback. `Page::fromBytes` validates and
copies the encoded page into its single allocation. Its views outlive the parser
and preserve the dictionary page limits. The reader's Rust driver owns its
1,024-byte input buffer and chapter file; `xp_book_parse` retains incremental
lifecycle decisions for the dictionary facade. Entity growth reuses the selected
driver's buffer, requesting a fallible replacement only when capacity is insufficient. The handle contains semantics,
assembly and the XML pull parser; XML events and CSS resolution stay inside Rust.
The private resource base and reserved semantic-pointer slot preserve the existing
300-byte native dictionary parser allocation on C3 and S3. The reader does not
allocate that wrapper; its Chapter is owned directly by the Rust build.

Production measurement and bidi execute in Rust. The synchronous engine borrows
font tables through a two-family descriptor cache. Chapter metrics borrow the
cache stored in the stable chapter handle rather than constructing it in each
metric frame. Other synchronous measurement entries retain their own cache. C++ owns the fonts,
facade storage and preparation; Rust owns SD cache policy and buffer lifetimes.
Nonresident glyphs and TTF work retain dynamic callbacks.
Table loans end before handlers that can mutate storage, and generation changes
invalidate descriptors before reuse. The generation counter relies on the existing
serialized owner; it does not make font access thread-safe. On single-core C3 its
increment uses the interrupt-masking `__atomic_fetch_add_4` helper. C++ also owns
the shared 3,072-byte bidi storage and mutex, leased exclusively to Rust during
measurement, ordering and glyph drawing. Rust resolves bitmap views and paints
glyphs; C++ routes bitmap misses through the font facades. SD misses use the Rust
pool; built-in compressed groups use the C++ cache and shared Rust inflater. Cached EPUB,
rich dictionary, settings preview and dictionary selection pages enter Rust through
`GfxRenderer::drawPage` once per render pass. Rust traverses the records and calls
C++ once per image element. Plain dictionary display still draws wrapped spans directly.
See the [page rendering contract](rust.md#page-rendering),
[glyph drawing contract](rust.md#native-glyph-drawing) and
[native measurement contract](rust.md#native-text-measurement) for
implementation links, generation obligations and differential tests. Host synthetic
renderers continue to use metric callbacks.

Text and style payloads are borrowed for one synchronous operation. Assembly
copies text into its paragraph storage and copies extracted line data into the
encoded payload buffer. There is no queued op
stream in firmware and no second copy of a paragraph. Replies must be consumed
before advancing the XML reader, whose event strings are borrowed.

The reader driver and retained native `parseStep` each read at most 1,024 input
bytes and drain that input's XML events.
It may emit several pages before returning. Splitting a callback into operations
must not introduce extra yield points. On the native backend a callback may call
`requestStop`; that
sets the C++ stop latch. The sink returns it after the callback, assembly records
the result, and semantics applies feedback after assembly returns. XML then stops
without retaining the borrowed event. It must not destroy state from inside the page callback.
`finishParse` flushes the remaining block/page only when not stopped and not
in layout OOM. `abortParse` releases XML, buffer and file without that flush.
`ReaderSection::suspend` keeps completed pages and aborts the Chapter; resuming
rebuilds from the start rather than serializing parser stacks. A saved partial
remains readable while the new build has fewer completed pages.

Builder allocation/capacity failure latches the existing layout OOM state and
prevents partial page delivery. Abort and finish destroy all payloads before the
chapter storage. The Section write failure still returns offset zero without
incrementing the built-page count, preserving its existing error path. Cache
headers, page bytes, trailers, partial-build markers and LUT order are unchanged.

Page loading first sizes the bounded file range through a 128-byte read cache.
It then allocates the Page header, Rust index, encoded bytes and compact rendering
descriptors together, reads bytes into their final location and validates them
before exposing views. Resident and peak requested page storage are identical.
The final validation checks the descriptor count against the sizing pass.
`Page::fromBytes` uses the same allocation but copies its borrowed source.
All load failures release the complete allocation. Preview and plain dictionary
selection build bounded page bytes and load them through this factory. TextBlock
is a borrowed view in firmware; owning constructors and legacy serializers remain
only in host codec/layout fixtures.

## Required operation families

Widths below describe the target contract, not packed C++ object layouts.
Most booleans and enum/style flags are u8. Existing exceptions use C `bool`: the
assembly options `extra_spacing`, `hyphenation` and `focus`, box `shaded`, the
`probe_image` popup pointer. Coordinates
and insets are i16, font IDs
are i32, font scale and compression are IEEE-754 f32. Counts crossing the
C ABI use explicit u32 rather than host `size_t`. Strings are borrowed UTF-8
byte spans with u32 length; absence is separate from an empty string.

| Operation | Request | Reply / effect observed by semantics |
| --- | --- | --- |
| BeginBlock | BlockStyle; paragraph-end u8; paragraph/list indices u16 | Existing empty block reuse, marker-only reuse, or new paragraph; OOM/stop; active style |
| Text | Bytes; style u8; attach/no-space u8; visible start u32; link/size slot u8 | Paragraph size, row fallback, allocation failure |
| EndBlock | Include-last-line and paragraph-end u8; visible offset u32; inline-size/drop-cap state | Remaining words after bounded layout, consumed drop cap, stop/OOM |
| LineBreak | BlockStyle; preserve-whitespace u8 | Paragraph state and indent suppression |
| Image | Source and alt bytes; CSS width/height (f32 + unit u8 + defined u8) | Probe success, intrinsic dimensions, resolved display dimensions; failure selects alt text |
| HorizontalRule | BlockStyle; visible offset u32 | Page/anchor changes; stop |
| PageBreak | Pending-after and current-before u8 | Consumes the request even if the page is empty; stop |
| BoxOpen / BoxClose | Depth i32, left/right/top/bottom padding i16, four border sides (u8 width/style), shaded u8 | Adjusted block padding; open-scope limit and close result |
| DropCap | Arm/capture/cancel; at most 12 text bytes, u8 length/codepoints/lines/bold, i32 span depth | Capture acceptance; layout consumes the captured letter |
| ListMarker | Bytes; visible start u32 | Marker-only paragraph state |
| TableBegin / Row / Cell / End | Direction u8, spans u16, cell style and border | Bounded row buffering or stacked fallback; paragraph state; stop/OOM |
| MeasureTable | Original XML byte position u64 | Success; column count u8; four min/preferred widths u16 |
| MeasureText | Font i32; text bytes; style u8 | Advance i32, with SD-font preparation before the query |
| FontMetrics | Ascender, line height, space width, SD-font test, scaled font variant | i32 metric/font ID or u8 SD-font result; preserve query order and f32 rounding |
| Ruby | Begin index i32, normalized annotation bytes, base count i32 | Paragraph size, continuation-style lookup, prior annotation bytes |
| SupSub | Inline style selection, represented in Text style bits | No separate page object |
| Anchor / Footnote | Anchor ID or bounded number/href bytes; word index i32 | Deferred anchor flush/page number u16; link ID u8 |
| SetVisibleOffset | Visible-codepoint offset u32 | First page clamps to zero; later pages retain their first offset |
| StopRequested | No payload | Sticky cancellation until a new parse begins |

Paragraph existence/size/style, indent suppression, links, size slots, ruby and
word insertion are direct Rust operations. They do not cross a C++ paragraph
wrapper. The captured `Paragraph.*` records and immediately following scalar
replies are filtered on both sides by the M11 comparator; metric replies nested
inside those calls remain observable. The explicit `RUST_PARAGRAPH_OPS` set in `test/section_golden/run.py` also lists
the migrated M10 assembly/scalar operations, including `createParagraph`,
`addPendingFootnote` and `setTableStacked`. Filtering is by those exact names;
nested metrics and all operations outside the set remain observable.

BlockStyle's ordered values are alignment u8, character spacing i8, nine i16
margin/padding/indent values, four u8 indent/alignment/direction flags, f32 font
scale, u8 scale-defined, i32 font ID, and three u8 page-break/keep-with-next
flags. A CssLength is f32 value plus u8 unit; a border side is u8 width plus
u8 style. Serialize fields explicitly; never compare or transport object padding.

## Compatibility details

Visible offsets count Unicode codepoints in body text, including source
whitespace. They are not original XML byte ranges. The XML byte index is used
separately for table read-ahead. Synthetic image alt text does not advance the
source counter. Normalization and discretionary word splits are downstream
paragraph responsibilities and retain their own source mapping.

A block start can reuse an empty paragraph, or a paragraph containing only a
list marker. Its vertical style merge differs from horizontal ancestor style
accumulation. Flushing at every block start changes pages. Soft flushes retain
the last line and continue the same paragraph; ruby collection delays them.

Table pre-scan has a 32 KiB scan bound, four columns, 39-byte word chunks and
47-byte tag text. Its simplified entity measurement and nested-table handling
are existing behavior. Rows use at most 160 tokens / 2,048 text bytes before
falling back to stacked flow. Preserve these distinct limits.

Float calculations remain observable: CSS length conversion, inherited and
inline font scaling, image aspect-ratio clamping and drop-cap sizing can change
integer pixels. Preserve operation order and casts rather than accepting an
epsilon. The host trace stores f32 bits; section/cache and device comparisons
remain necessary because host fonts and image decoding are stubbed.

## Direct calls and sink callbacks

`XpChapterServices` has been removed. `chapter/binding.rs` calls assembly directly
and takes a temporary `Flow` snapshot of semantic scalars for the operation. It
writes changed marker, continuation, inline-size, drop-cap, page-break and cell
state back before the next event. The snapshot is not a second persistent state.
Closing a cell pops the actual semantic block stack before layout, and image
handling separates probing/sizing from placement so pending text is flushed at
the original point without reborrowing active assembly storage.

The reader's stable `BuildServices` remain alive until Chapter destruction. The
retained dictionary `XpChapterSink` and metrics/context obey the same lifetime.
The `ChapterHtmlSlimParser` facade operations forward to `xp_book_parse`,
which drives the `xp_chapter_begin` / `xp_chapter_feed` / `xp_chapter_finish`
lifecycle. Table pre-scan is the only callback that receives the semantic-state
pointer: `table_file` lends a local `XpChapterReader` whose lifetime ends at
return, and Rust's `use_reader` in `chapter/table.rs` runs open, seek, measure
and column planning as the only reader of the active state. Neither the state,
the reader nor the read buffer may outlive that call. Other callbacks cannot
reenter, feed or destroy the handle. Required transitions are:

| Operation | State changed before return |
| --- | --- |
| `startNewTextBlock` | Clears continuation; resets inline-size state for empty reuse or a fresh paragraph; consumes marker-only state; disarms first-letter capture for a fresh paragraph; may lay out text and flush anchors |
| `flushPendingAnchor` | Moves pending ID into the selected backend's completed-anchor storage with page u16; clears pending ID; may trigger a TOC page break and cancellation |
| `openBoxScope` | Flushes a pending word before geometry; may start a block; adds border/gap widths to the borrowed BlockStyle's padding |
| `closeTableCell` | Clears inside-cell u8; pops the cell's semantic block style; latches layout OOM before moving paragraph ownership |
| `finishTableRow` | Closes the cell first; may consume buffered paragraphs, emit pages, and observe stop/OOM |
| `makePages` | Consumes pending page-break u8; consumes captured drop-cap state when laid out; latches OOM; page callback can set stop |
| `emitHorizontalRule` / `processImage` | Flushes semantic text only at the original placement point; performs block-transition feedback; may clear deferred anchors; image returns Fallback/Displayed/Aborted |
| `beginAssembly` | Resets stop/html-end, root block stack, drop-cap/list/table state, then opens the initial paragraph |
| `finishAssembly` | Flushes the last paragraph, records any deferred anchor, emits the last page, and returns failure on stop/OOM |

Assembly reads XPath indices (u16), visible offset (u32), alignment and whitespace,
marker-only and cell-open flags, table depth/border/column widths, ancestor style,
inline-size state and the captured drop cap. The pending word is flushed before
entering an eligible box or horizontal-rule operation. No mutable paragraph
borrow remains active while the borrowed-line callback reads `lines_after`.

On the retained native backend, `requestStop` sets the C++ driver latch. The completing sink reports it after
`completePageFn` returns; Rust then stops extraction through its local stop byte
and latches cancellation in assembly and semantics. A stopped completion consumes
all handed-off payloads; Rust drops retained carry/table payloads on abort. No
reverse semantic notification runs while page assembly has an active borrow.

## Allocation and tracing

The chapter handle is initialized in place after a single fallible allocation.
Semantic block/inline/list/ruby growth uses `try_reserve`; allocation failure
latches OOM and fails the section instead of caching partial output. No semantic
handle survives `abortParse` or the final assembly flush. An empty stylesheet
store is distinct from having no CSS parser: inline styles still apply when the
parser exists but has not allocated a store. CSS is loaded before chapter creation
and its borrowed store must remain alive and unchanged until chapter destruction.

Word storage is 201 bytes (200 plus NUL), drop-cap storage 13, footnote label 32,
and retained href 256. CSS ancestors are bounded at 16 with four class hashes per
ancestor. Tag/class/id spans borrow the XML event storage; no extra per-event
copy or independent truncation is introduced. Existing Ruby, block/style/list,
and image-alt buffers grow fallibly when needed. Ruby and image-alt text retain
the C++ 15-byte short-string storage. Entity spellings reuse the driver input
buffer after XML has copied its input; the existing buffer grows fallibly only
when an entity exceeds its capacity. The table scanner
uses 96 input bytes, 40 word bytes and 48 tag bytes on its stack.

`chapter-trace` and `chapter-test` Cargo features are enabled only by the native
test CMake configuration. Firmware's `scripts/build_rust.py` supplies neither.
Tracing emits selected semantic observations through a synchronous callback;
assembly emits its original observations and metric replies through a borrowed
byte callback. The host instrumenter attaches the paragraph layout stream and
records the driver lifecycle; Rust emits the placement metric observations. Test-only
entry points preserve direct-callback assertions, including state checks, without
adding per-event FFI entry points to firmware. The original C++ assembly implementation is retained in Git history and the
captured operation archives.

The entity vocabulary is shared with the existing C++ `HtmlToPlainText` and
KOReader progress mapper through `entity_lookup`. Rust owns expansion and unknown
entity preservation; this rare immutable-data query avoids a second vocabulary.
It does not forward XML start/end/text events to C++. The query returns a static
UTF-8 span or null for an unknown spelling and performs no allocation.
