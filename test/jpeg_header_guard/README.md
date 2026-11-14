# JPEG header guard

`JpegHeaderGuard` checks `scripts/jpegdec_patches/0004-validate-jpeg-header-segments.patch`
in the default host suite. It fetches the firmware JPEGDEC pin and builds three
copies with AddressSanitizer and UndefinedBehaviorSanitizer:

- `guarded`: all firmware patches, unchanged (the shipped decoder);
- `guarded_observed`: the same source with observer hooks;
- `control_observed`: all patches except 0004, with the same hooks.

`make_variant.py` inserts each hook just before a statement that uses a file
field: the component array write, the marker and segment reads, table parsing,
EXIF reads and the call that follows the scan-header check. A hook reports
only when that use is unsafe for the current input. Hooks sit after the guard
code, so the guarded copy must never report. The firmware has no hook.
Two inherited UB reports that occur for every file are excluded: alignment
checks are off (the decoder reads unaligned 16-bit words), and
`ubsan-ignore.txt` exempts the Huffman lookup builder's negative shifts. The
header parser itself stays fully checked.

`HeaderGuardTest.cpp` encodes a 24x16 baseline image and builds every input
from it at run time; there are no binary fixtures. `run_cases.py` runs each
case in its own process for each copy:

- malformed headers (component counts 0/5/255, short SOF, zero/one-byte
  segments, lengths past the end of the file, truncated segments and markers,
  a DHT larger than the buffer window, bad DHT ids, DQT underflow, bad scan
  headers): the shipped decoder returns `JPEG_DECODE_ERROR` with no sanitizer
  report. The control must reach the named unsafe use; the report records
  whether it then crashed, hung, accepted the header, or failed later;
- malformed EXIF: the shipped decoder skips the block and decodes the same
  pixels as the image without EXIF;
- valid files (combined tables, 16-bit DQT, restart intervals, progressive
  DC scan, 60 KB APP2, 40 KB EXIF, segments around the 1,024-byte refill
  point) and the existing image corpus: the shipped decoder returns exactly the
  control's result, including its file reads, bytes read and seeks;
- unusual files (fill bytes, long SOF, trailing DHT bytes, a stray RST marker):
  same result as the control, except that the shipped decoder may reject a
  header that the control "opened" without a usable frame (0x0).

Configure with `-DJPEG_HEADER_GUARD_MUTANTS=ON` to also build one decoder per
disabled guard hunk; running `run_cases.py` with a mutant as both guarded
copies must fail.
