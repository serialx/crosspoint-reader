# C++ hyphenation reference outputs

The default Rust and C++ golden tests use 652 distinct byte strings and 15,648
ordered break lists. The selection includes 64 words from each of nine corpora
plus every handwritten edge input in `prepare_golden_words.py`. The corpus resources are
already 1.12 MB, so this sample keeps this whole directory below 1.5 MB.

For each corpus, rank unique words by SHA256 of the bytes
`crosspoint-hyphenation-v1\0<corpus filename>\0<word>` (raw word bytes break ties),
then take the first 64. Union those words with all edge inputs, deduplicate, and
retain the full capture's input order. Edges cover explicit/soft hyphens,
apostrophes, footnotes, word-length limits, combining marks, CJK and malformed
UTF-8. Hex encodes the inputs; `-` denotes an empty input. Comment lines start
with `#`.

The break file keeps one tag per distinct behavior (eleven language IDs and the
unknown language) and both fallback flags. Each row is
`tag fallback wordIndex [byteOffset:insertHyphen ...]`. `golden-tags.txt` records
all 31 original tags' registry IDs, captured from the original C++ resolver;
`-` denotes the empty tag. Both test suites check that mapping separately.

The full reference is an external evidence archive, `m1-goldens-full`, containing
`golden-words.txt`, `golden-breaks.txt.gz`, the capture executable and sources,
and a manifest with SHA256 hashes of the uncompressed files. It has 43,471 inputs
and 2,695,202 ordered break lists. The compact outputs are filtered from that
C++ reference, never regenerated from the Rust implementation.

Regenerate the compact files with:

```sh
python3 test/hyphenation_eval/prepare_golden_words.py --full-dir /path/to/m1-goldens-full
```

`--output-dir /tmp/hyphen-subset` allows comparison without replacing fixtures.
To verify the full archived set instead of the default compact set:

```sh
XP_HYPHEN_GOLDEN_FULL=/path/to/m1-goldens-full cargo test --manifest-path rust/Cargo.toml -p xp-core
XP_HYPHEN_GOLDEN_FULL=/path/to/m1-goldens-full build/test/hyphenation_eval/HyphenationEvaluationTest --gtest_filter='HyphenationGolden.*'
```

The Rust full path requires `gzip` on PATH; the C++ host test links zlib and reads
the archive directly. Neither affects firmware dependencies or behavior.
`GoldenDump.cpp` is the original capture tool: build it with the archived C++
implementation, not the Rust facade, when checking reference provenance.
