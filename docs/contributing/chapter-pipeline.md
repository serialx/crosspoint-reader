# Chapter pipeline boundary

The chapter parser consumes XML events and CSS rules, then builds paragraphs
and pages. A Rust semantic layer must preserve the order of its requests to
the C++ page assembler, including requests whose replies influence later
parsing. The chapter operation oracle in `test/section_golden` records the
existing C++ implementation before the split.

## Ownership and lifetime

Semantics owns element depth, hidden-content tracking, CSS ancestors and style
stacks, word fragments, inline continuation, lists, ruby collection, link-label
collection, and visible-text/XPath counters. Assembly owns paragraph storage,
font selection and metrics, layout, page elements, image I/O, and completed-page
callbacks. The driver owns the XML handle, input buffer, chapter file, and
incremental parsing lifecycle.

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
are i32, font scale and compression are IEEE-754 f32. Counts crossing a future
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
requests and replies in the oracle. Existence, creation, reset, and row-storage
mutations are not uniformly recorded; complete section and layout comparisons
remain separate required checks.

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

`ChapterSemantics` is an abstract semantic state machine. `ChapterPageAssembler`
implements its synchronous virtual services, and the final
`ChapterHtmlSlimParser` adds XML/file driving. The objects are base subobjects
of one parser allocation. There is no separately allocated sink, context, or
queued operation. `ChapterTableSource::read` borrows a destination buffer and
returns the byte count; the driver wraps its stack-local table file.

The current C++ split retains in-place semantic state feedback from assembly.
This is part of the contract a later Rust adapter must expose, not permission
to omit these updates when translating the virtual calls:

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
ancestor block styles, pending/TOC anchor strings, inline-size state, and captured
drop cap. Inline-size state is f32 scale, i32 depth, u8 valid/open. Drop-cap state
is 13 text bytes (12 plus NUL), u8 byte count/codepoint count/line count/bold/
first-letter-pending, and i32 span depth. Stop and layout-OOM are sticky u8
feedback values. Assembly now uses explicit `semantic*` queries and notifications;
it has no friendship access to semantic fields. Queries return scalar values or
style/drop-cap copies; mutations name the transition, such as closing a cell,
consuming a marker/page break, clearing inline-size state, or recording an anchor.
The existing layout cancellation pointer is a read-only borrow for synchronous
layout. The driver still owns lifecycle access until the Rust handle replaces it.
These C++ queries define the effects to transport, not C++ container layouts for
the future C ABI.

The scalar paragraph services are `paragraphExists`, `paragraphEmpty`,
`paragraphSize`, `paragraphStyle`, `setParagraphStyle`, `createParagraph`,
`resetParagraph`, `suppressFirstLineIndent`, `ensureRubyCapacity`, `addWord`,
`addLinkTarget`, `linkTargetMatches`, `sizeSlotFor`, `getWordStyleAt`,
`getRubyTextAt`, `setRubyGroupAt`, and `setRubyForWordAt`.
`paragraphStyle` returns a borrowed mutable BlockStyle valid until paragraph
replacement; `getRubyTextAt` retains the existing C++ value-copy behavior.
Neither may become a long-lived Rust borrow across a paragraph mutation.

Row-storage services are `clearTableCells`, `reserveTableCells`,
`bufferedTableWords`, `tableStacked`, `setTableStacked`, `setTableRtl`,
`resetTableBytes`, `tableBytes`, `addTableBytes`, `resetExtractedWords`,
`extractedWords`, and `addPendingFootnote`. `boxAtDepth` returns a boolean.
Metric services are `bodyAscender`, `bodyIsSdFont`, `bodySpaceWidth`,
`prepareBodyText` (text span and u8 font-style mask), `bodyTextAdvance` (text span
and style u8), `pageWidth`, and `pageHeight` (u16). `applyParagraphDirection`
changes direction flags only while the current paragraph is empty.
