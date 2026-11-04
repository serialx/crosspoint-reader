# Actual reader Section fixtures

`ReaderSectionFixture` prepares Book 10 metadata and a stored ZIP from the existing
public fixture inputs, then constructs the actual Rust ReaderBook and ReaderSection.
The C++ wrapper owns no parser or parallel build state. Its operations forward to
app-test-only typed Rust methods; the physical leaves are extracted from the native
implementation. The shipping ABI has no fixture entries.

## Image-path oracle map

The original `test/page_golden/golden.json.gz` is unchanged. Its four
`test_tables/public/{0,1,2,3}` cases contain one historical fixture Page (`0/0`):
`EPUB/text/../media/cover.png` was preserved by the host's identity `normalisePath`.
Production normalizes this to `EPUB/media/cover.png`, shortening that Page from 68
to 60 bytes. The placement, dimensions and pixel hashes are unchanged.

The map has two independently exercised sides:

- `PageGoldenHistoricalImagePaths` reconstructs the four original 68-byte Pages
  from the immutable golden's complete image-element sample, checks both original
  byte hashes, and runs the retained native Page loader, serializer, query and
  render paths. It compares all original Page records and pixels. These are
  explicitly historical stub fixtures, not production Section output.
- `PageGolden` constructs the actual owners. For those four Pages it uses the
  parent production records in `page_image_parent.json.gz`; all other original
  expected records remain in force. The same reference also checks serialized
  bytes for all 739 Pages and all 488 persisted cache files in the 28 image-bearing
  cases, including the other image formats, display-none, kerning and synthetic
  cases. No actual result is rewritten or normalized by the comparator.

The parent reference was produced at `7f5e1cb5184d03957f1787ed06d872fab773b6db`
with SDK `6a573acd0562e45e4ce96837e0c2aebada219760`: native Section, native
ChapterPlatform, real FsHelpers, actual ReaderBook and its forwarding native Epub,
real native Page, and the existing PageGolden font/render environment. Parent and
candidate consumed the same stored ZIP and Book metadata. Every resulting Page,
render record and persisted cache file matched. The reference retains Page and
cache hashes instead of duplicating all existing rendering goldens.

Review material kept outside this repository records the isolated parent source
modifications, production-source identity checks, binaries, raw outputs and full
comparison. It proves that the actual parent and candidate produce the same Page
and cache bytes; the failed harness attempts and initial failing candidate run
are retained separately. Do not regenerate either historical goldens or the
parent reference to resolve a mismatch.

## Other identity-path consumers

Compiler include-order inspection found the same identity header in
ChapterHtmlSlimParserTest, ChapterDiagnostics and LineRenderGolden, as well as
PageGolden. The first two retain the native dictionary-compatible Chapter adapter;
their current image inputs are simple `probe.gif`, `failed.gif` and
`unsupported.svg` paths. LineRenderGolden includes the PageGolden translation unit
for font/render helpers, but its own main builds plain text lines and does not call
`pageGoldenMain` or build an EPUB Section. These remain their existing fixture
contracts, with no additional persisted-path exception.

SectionGoldenDump, PageLoad, PageCodecDiff, SectionIoOracle, SectionBehavior and
page/section diagnostic targets resolve the real FsHelpers header before the
identity stub. The Section byte/read goldens therefore need no image-path
reclassification. The content-OPF parser has a separate identity header; it does
not produce Section/Page image bytes. The web-jobs helper performs actual path
normalization and is not this stub.

## Chapter and layout observation

SectionGoldenDump links a separate host archive built from a generated Rust
workspace. `instrument.py` checks each insertion site is unique, forwards the
existing semantic/layout observations, and records Begin/Step/Finish at the actual
reader driver entries. The native trace formatter remains shared with the retained
Chapter adapter. No return value or policy expression is replaced.

The scratch manifest hashes every copied Rust source, its instrumented form and the
build inputs. Only `section_owner/{mod,services,driver}.rs` differ from shipping
source, plus the generated observer imports. The ordinary host archive and shipping
sources have no observer imports. SectionGoldenDump links exactly one Rust archive;
PageGolden and the independent real-owner compositions use the ordinary archive.
The original 312 section, operation and layout traces remain unchanged.
