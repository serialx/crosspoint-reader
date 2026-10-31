# Dictionary engine oracle

`python3 test/dictionary_engine/oracle.py` compiles the production engine with an
in-memory HAL and compares its results and formatted diagnostics with `golden.txt`,
captured from commit `31d5cd46`. The fixtures are synthetic and contain no private
dictionary data. `--check-mutation` proves the oracle rejects a deliberate normalization mutation
(the original C++ wrapper, or zero input length in the Rust facade); `--source-root PATH` supports an exported original source tree.

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

`python3 test/dictionary_engine/abi.py --check-mutation` checks all named ABI
values in C++ and Rust, rejects deliberately mismatched buffer, sampling and
operation values, and exercises scan/refill buffers and entry lists with smaller
owner capacities. It runs as `DictionaryAbiContract` in CTest.

`python3 test/dictionary_engine/oracle.py --compressed` checks the separate
16-row `golden-compressed.txt`, also captured from `31d5cd46`. A dictionary with
only `.dict.dz` data returns a definition spanning all three chunks, reports a
corrupt first-chunk extraction, and succeeds after restoring the data on the same
Dictionary instance. The oracle records status, headword, definition and temporary
file bytes. It runs as `DictionaryCompressedLookupOracle` in CTest.
