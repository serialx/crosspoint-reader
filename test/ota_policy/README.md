# OTA policy oracle

`golden.txt.gz` and `usb-golden.txt.gz` were captured from the actual C++ implementation at
`386e3c45108357d8445c0b21410e072045970594`, before the Rust port. Platform leaves are
scripted; the release JSON parser and board scanner remain real. The tests compare
ordered leaf calls, byte hashes, result codes, progress, and formatted diagnostics.

The network/boot suite covers 210 cases plus 3,059 defined version comparisons. It
includes chunk boundaries across the image header and board tag, wrong targets,
empty/truncated transfers, failures before and after writes, verification and boot
selection failures, repeat checks, progress throttling, and OTA slot selection.
Power-loss probes run separate processes that terminate at each leaf boundary;
they never unwind through Rust. A boot switch preserves the old active slot. The
fake OTA verifier rejects incomplete images, and boot selection asserts successful
verification. Hardware flash atomicity and ESP-IDF image validation require device
tests; this suite verifies the application's ordering and error handling.

`versions.inc` includes repository tags, `platformio.ini` version suffixes,
`scripts/git_branch.py` development strings, and the release-parser JSON example.
The original comparator reads uninitialized integers for some malformed versions;
those comparisons are excluded from the old-code golden. Rust unit tests cover the
approved deterministic rejection. Defined partial comparisons such as `2` versus
`1.2.3` remain in the golden. The USB suite covers successful reset, semaphore and
interrupt allocation failure, and reset timeout.

Run through CTest (`ota_policy_oracle`, `ota_usb_oracle`) or directly:

```sh
python3 test/ota_policy/run.py --build build/ota-policy --archive PATH_TO_HOST_LIBXP_FFI_A
python3 test/ota_policy/run.py --build build/ota-usb --archive PATH_TO_HOST_LIBXP_FFI_A --usb
```

`--source` selects an exported original tree for differential testing. `--record`
regenerates goldens and must only be used with the original implementation. The
sensitivity check changes the original `processedSize += len` to add one extra
byte; the golden comparison must fail.

The existing `web_jobs_oracle` covers the already-ported SD flasher, image checks,
board scanner and download policy. OTA UI screens and real device restart behavior
are outside this host suite.
