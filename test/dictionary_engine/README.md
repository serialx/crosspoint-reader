# Dictionary engine oracle

`python3 test/dictionary_engine/oracle.py` compiles the production engine with an
in-memory HAL and compares its results and formatted diagnostics with `golden.txt`,
captured from commit `31d5cd46`. The fixtures are synthetic and contain no private
dictionary data. `--check-mutation` proves the oracle rejects a one-line change to
the XHTML wrapper; `--source-root PATH` supports an exported original source tree.

The cases cover sampled-index boundaries, duplicate headwords, synonym ordinals,
English stem fallback, case and punctuation cleaning, stale/missing sidecars,
interrupted reads/writes, checked-allocation refusal, zero/large entries, registry
filtering, HTML normalization bytes, and dictzip header/truncation/chunk mutations.
Large output records include their length and FNV-1a hash; short records include
all bytes. Time is fixed at zero. The HAL preserves partial writes and exposes
explicit open/seek/read/write faults. Heap and checked C++ allocations can fail;
ordinary host STL allocations are not fault-injected.

This oracle does not establish display parity, device timing, or actual ESP32
allocator pressure. The existing inflate and styled-page tests remain necessary.
Regenerating the golden requires `--write-golden`; do so only against the agreed
original implementation, before changing its behavior.
