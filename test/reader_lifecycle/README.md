# Reader lifecycle oracle

The compressed golden records 8d534a99e53f3bc98d842740de606b0396d38d56 before
porting reader policy. `extract.py` compiles selected production activity
methods against deterministic clock, lock, heap, storage and display leaves.
It does not keep a second implementation of those methods.

The trace covers session dwell thresholds and clock wrap, chapter/page turns,
queued turns during rendering and the 200 ms manual-turn gap, chapter skipping,
radio/build admission, background build failure and framebuffer loans,
progress bytes and failure ordering, link-stack persistence, refresh cadence,
link-note repaint, gesture settings and language detection. Progress cases
include old four-byte files, six- and ten-byte files, truncated/oversized reads,
stale last-page sentinels, invalid write values and temporary-file failures.

Run the `reader_lifecycle_oracle` CTest after building `ReaderLifecycleOracle`.
The extractor follows the production source at build time; candidate output
must match the frozen golden. Never regenerate the golden from a port.
Changing the extracted `section->currentPage++` to `+= 2` fails the comparison.

These host tests supply captured input at method boundaries. They do not model
GPIO debounce or prove that a physical tap wholly inside a blocking build is
sampled. Emulator input scripts and paired screen/heap/timing captures remain
necessary for the complete reader migration. Storage leaves record ordering and
bytes; they do not simulate FAT power-loss recovery. The display leaves record
requested refresh policy, not controller waveforms.
