# Chapter pipeline boundary

## Phase 3 assembly design (integration pending)

`chapter::assembly` will own the current paragraph, ordered page elements and
positions, box scopes, six carried-line offsets, orphan/widow and keep-with-next
state, buffered table paragraphs and column lines, pending footnotes, deferred
and TOC anchors, completed anchor records, visible offsets, image counter/popup
latch, and stop/OOM latches. Semantics and assembly share one fallibly allocated
chapter handle. Phase A tests assembly with a paragraph trait; Phase B replaces
that adapter with M11's `para::paragraph::Paragraph`. The existing C++ path stays
active until the coordinator supplies the verified M11 commit.

The remaining C boundary is a synchronous page sink:

| Sink operation | Contract |
| --- | --- |
| Create text | Consume M11's borrowed line layout during the callback and construct the existing `TextBlock`; return an opaque owned payload plus placement metrics and borrowed link spans. |
| Process image | Keep path resolution, format checks, header probing, framebuffer loans, extraction, decoder retries and failed-cache removal in C++; return intrinsic dimensions and a temporary image resource. Rust computes CSS sizing and placement in the original order. |
| Create image | Construct the existing `ImageBlock` from that resource and chosen dimensions; return an opaque owned payload. |
| Drop payload | Return every unconsumed text/image payload to its C++ allocator exactly once, including stop, OOM, discarded table lines and abort. |
| Font metrics | Resolve scale to font ID; query ascender, compressed/uncompressed line height, space width, codepoint metrics, SD-font status, text preparation and advance. Reuse M11's metrics contract where signatures agree. |
| Complete page | Borrow ordered records (kind, i16 x/y and payload), links, footnotes, u16 paragraph/list indices and u32 visible offset. C++ builds `Page`, consumes every opaque payload, and invokes `completePageFn` synchronously. Failure must consume/drop all supplied payloads too. |
| Popup | Show the existing indexing popup at the original probe/extraction point. The image single-shot latch is Rust-owned and distinct from the driver's large-file popup. |

Rules, drop caps, boxes and table separators are plain records until page
completion. Strings/spans are borrowed for one call; stored anchors/links own
their bytes. Widths remain those listed below: u8 flags/kinds, i16 coordinates,
i32 font metrics/IDs, f32 scales/compression, and u32 counts. A record never
transfers a Rust allocation to C++. A callback may latch stop but cannot feed,
destroy, or recursively lay out the handle. The active Rust borrow must end
before observing that stop latch. Completed-page elements are consumed even when
the callback requests stop; carried elements remain Rust-owned until transferred
or dropped.

Rust preserves eight box scopes, six carried lines, four table columns and the
160-token/2,048-byte row fallback. Dynamic buffers use `try_reserve`; their owner
is the chapter and their lifetime ends on completion/abort. Page storage grows
only as the original page grows; table scratch is retained and cleared between
rows. No second paragraph text copy or full-framebuffer allocation is added.
Exact C3 sizes, transient growth peaks and allocation counts remain measurement
gates, not design claims.

The existing 312 section streams and 5,529 layout calls remain the output
oracles. `chapter-trace` must emit the old operation names, replies, field order
and f32 bits at the corresponding direct Rust calls. Paragraph recording follows
M11's approved filter; removing an FFI thunk alone does not justify dropping a
semantic reply. Page records, image dimension/sizing observations, block/box
changes, table results, anchors and stop feedback remain observable. The frozen
C++ instrumented executable is retained before porting; no extra element oracle
is needed because the section read oracle already exposes each element and its
coordinates. Private operation streams remain external evidence.

`parseStep` still consumes at most 1,024 file bytes and may emit several pages.
Finalization flushes only while neither stopped nor OOM; abort releases pending
payloads without flushing. Suspend retains completed pages and reconstructs the
parser on resume. Table pre-scan still borrows a synchronous file reader. The
image sizing branches must retain their distinct round-to-nearest versus
truncation operations, and carry logic must never move every element off a page.

The chapter parser consumes XML events and CSS rules, then builds paragraphs
and pages. The Rust semantic layer preserves the order of its requests to
the C++ page assembler, including requests whose replies influence later
parsing. The chapter operation oracle in `test/section_golden` records the
C++ implementation captured before the port.

## Ownership and lifetime

Semantics owns element depth, hidden-content tracking, CSS ancestors and style
stacks, word fragments, inline continuation, lists, ruby collection, link-label
collection, and visible-text/XPath counters. Assembly owns paragraph storage,
font selection and metrics, layout, page elements, image I/O, and completed-page
callbacks, deferred anchor strings, and the anchor deque. The driver owns one
fallibly allocated Rust chapter handle, the 1,024-byte input buffer, chapter file,
and incremental parsing lifecycle. The Rust handle contains semantic state and
the XML pull parser; XML events and CSS resolution stay inside Rust.

Text and style payloads are borrowed for one synchronous operation. Assembly
copies text only into its existing paragraph storage. There is no queued op
stream in firmware and no second copy of a paragraph. Replies must be consumed
before advancing the XML reader, whose event strings are borrowed.

`parseStep` reads at most 1,024 input bytes and drains that input's XML events.
It may emit several pages before returning. Splitting a callback into operations
must not introduce extra yield points. A callback may call `requestStop`; that
sets a latch, and the driver stops XML only after the active callback releases
its borrowed event. It must not destroy state from inside the page callback.
`finishParse` flushes the remaining block/page only when not stopped and not
in layout OOM. `abortParse` releases XML, buffer and file without that flush.
`Section::suspendBuild` keeps completed pages and aborts the parser; resuming
rebuilds the parser rather than serializing its stacks.

## Required operation families

Widths below describe the target contract, not packed C++ object layouts.
Booleans and enum/style flags are u8, coordinates and insets are i16, font IDs
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

The current source also needs paragraph services: existence/empty/size queries;
get/set BlockStyle; reset/suppress first-line indent; link-target matching and
insertion; size-slot lookup; ruby capacity/group/word access; and allocation or
reset of the current paragraph. These cannot be hidden as direct access to a
C++ `ParsedText` from Rust. The instrumented subset is recorded as `Paragraph.*`
requests and replies in the oracle. The expanded baseline also records paragraph
creation/reset, row storage, and metric services. Complete section and layout
comparisons remain separate required checks.

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

## C++ service surface and state feedback

`xp-core/src/chapter` owns the semantic state. `XpChapterServices` in `xp_ffi.h`
is the C boundary to `ChapterPageAssembler`; each function pointer receives the
assembler context and a fresh synchronous semantic-state reborrow. The table is
static, not copied into each handle. `ChapterHtmlSlimParser` drives the handle
with `xp_chapter_feed` and owns file I/O. Table pre-scan uses a synchronous read
callback over a stack-local HalFile; it never retains the file or buffer.

Assembly may query values or issue named state notifications during a service
call. It must use the state pointer supplied to that call, never an older handle
borrow. `SemanticBorrow` installs that pointer only for operations that can reenter
semantics and restores the previous pointer after nested calls. Leaf paragraph
and row-storage services do not read or mutate Rust state. The
only reverse semantic operation that emits further services is `FlushWord`;
no Rust vector reference is retained across those services. Callbacks must not
feed or destroy the chapter handle. These feedback effects remain required:

| Service | Semantic state changed before return |
| --- | --- |
| `startNewTextBlock` | Clears continuation; resets inline-size state for empty reuse or a fresh paragraph; consumes marker-only state; disarms first-letter capture for a fresh paragraph; may lay out text and flush anchors |
| `flushPendingAnchor` | Moves pending ID into the anchor deque with completed-page u16; clears pending ID; may trigger a TOC page break and cancellation |
| `openBoxScope` | Flushes a pending word before geometry; may start a block; adds border/gap widths to the borrowed BlockStyle's padding |
| `closeTableCell` | Clears inside-cell u8; pops the cell's semantic block style; latches layout OOM before moving paragraph ownership |
| `finishTableRow` | Closes the cell first; may consume buffered paragraphs, emit pages, and observe stop/OOM |
| `makePages` | Consumes pending page-break u8; consumes captured drop-cap state when laid out; latches OOM; page callback can set stop |
| `emitHorizontalRule` / `processImage` | Flushes semantic text only at the original placement point; performs block-transition feedback; may clear deferred anchors; image returns Fallback/Displayed/Aborted |
| `beginAssembly` | Resets stop/html-end, root block stack, drop-cap/list/table state, then opens the initial paragraph |
| `finishAssembly` | Flushes the last paragraph, records any deferred anchor, emits the last page, and returns failure on stop/OOM |

Assembly reads the current XPath indices (u16), visible offset (u32), paragraph
alignment (u8), whitespace mode (u8), marker-only flag (u8), table depth (i32),
cell-open flag (u8), table border, four planned widths (u16), column count (u8),
ancestor block styles, inline-size state, and captured
drop cap. Inline-size state is f32 scale, i32 depth, u8 valid/open. Drop-cap state
is 13 text bytes (12 plus NUL), u8 byte count/codepoint count/line count/bold/
first-letter-pending, and i32 span depth. Stop and layout-OOM are sticky u8
feedback values. Scalar queries return values and block/drop-cap queries copy
fields. Notifications name the transition, such as closing a cell, consuming a
marker/page break, or clearing inline-size state. C++ retains the bool stop latch
borrowed by the existing synchronous layout routine; `requestStop` also updates
the current Rust state reborrow.

The scalar paragraph services are `paragraphExists`, `paragraphEmpty`,
`paragraphSize`, `paragraphStyle`, `setParagraphStyle`, `createParagraph`,
`resetParagraph`, `suppressFirstLineIndent`, `ensureRubyCapacity`, `addWord`,
`addLinkTarget`, `linkTargetMatches`, `sizeSlotFor`, `getWordStyleAt`,
`getRubyTextAt`, `setRubyGroupAt`, and `setRubyForWordAt`.
The C boundary copies BlockStyle values. Font-scale-only queries return f32
directly, avoiding a full style copy on every word; the host oracle still records
the same underlying paragraph-style query and reply. `preserve_space` combines indent
suppression and justify-to-left mutation in C++, preserving the original service
order without exporting a mutable C++ reference. `ruby_append` reads and joins
the existing annotation in C++, preserving its original string allocation.
`list_marker` preserves the existing five-argument paragraph insertion. The
link callbacks borrow NUL-terminated XML/footnote storage as well as its explicit
length; other spans need no terminator and word insertion uses the length.

Row-storage services are `clearTableCells`, `reserveTableCells`,
`bufferedTableWords`, `tableStacked`, `setTableStacked`, `setTableRtl`,
`resetTableBytes`, `tableBytes`, `addTableBytes`, `resetExtractedWords`,
`extractedWords`, and `addPendingFootnote`. `boxAtDepth` returns a boolean.
Metric services are `bodyAscender`, `bodyIsSdFont`, `bodySpaceWidth`,
`prepareBodyText` (text span and u8 font-style mask), `bodyTextAdvance` (text span
and style u8), `pageWidth`, and `pageHeight` (u16). `applyParagraphDirection`
changes direction flags only while the current paragraph is empty.


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
the host instrumenter also records assembler services and replies. Test-only
entry points preserve direct-callback assertions, including state checks, without
adding per-event FFI entry points to firmware. The original C++ implementation
was removed after 312 public and 111 private operation streams matched.

The entity vocabulary is shared with the existing C++ `HtmlToPlainText` and
KOReader progress mapper through `entity_lookup`. Rust owns expansion and unknown
entity preservation; this rare immutable-data query avoids a second vocabulary.
It does not forward XML start/end/text events to C++. The query returns a static
UTF-8 span or null for an unknown spelling and performs no allocation.
