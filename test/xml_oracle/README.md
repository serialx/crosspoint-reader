# Vendored XML oracle

This host-only tool records the XML contract of the firmware's vendored Expat
from `test/vendor/expat` with `XML_GE=0` and `XML_CONTEXT_BYTES=0`. It does not use system Expat.

```sh
cmake -S test/xml_oracle -B build/xml-oracle
cmake --build build/xml-oracle
ctest --test-dir build/xml-oracle --output-on-failure
```

`corpus/` contains handwritten compatibility cases and container, encryption,
nav and NCX members found in each EPUB under `test/epubs/`. `sources.json`
records each source member and SHA-256. No private harness books are included.
To deliberately replace the fixtures after reviewing a parser change:

```sh
python3 test/xml_oracle/oracle.py --oracle build/xml-oracle/XmlOracle record
```

Goldens use deterministic gzip compression; the Python loader decompresses them
in memory. Each golden contains unique result `variants` and a run-length encoded
`schedules` mapping. Each pair `[count, variant_index]` repeats that result for
`count` consecutive schedules. `splits` feeds two slices at every byte boundary,
including zero and EOF; `chunks_N` uses chunks of N bytes. `stop_N_*` requests a
non-resumable stop when the Nth recorded event starts. Adjacent text callbacks
are coalesced without crossing other semantic events. The first callback's
original byte offset and line identify a coalesced text run. A stop can expose
partial text, since it occurs inside the callback before later callbacks.

An event is `[kind, byte_index, line, ...data]`. Start events contain the raw
qualified name followed by alternating attribute names and values, in Expat's
order. End events contain the name; text/comment/entity contain one string; PI
contains target and data; CDATA boundaries have no data. Only entity references
are retained from the default handler, not DTD syntax or document whitespace.
The remaining fields are Expat's numeric status/error, current byte/line,
parsing state, and final-buffer flag. These flags can differ by schedule on
errors or stops; the oracle preserves those differences.

The executable also supports direct inspection:

```sh
build/xml-oracle/XmlOracle test/xml_oracle/corpus/declared_entity.xml chunks 7 0
```

The final argument is the stop-event ordinal, or zero to parse to completion.

## Observed compatibility

- The five predefined entities and valid numeric references decode in text and
  attributes. Literal CR/CRLF normalize to LF in text and spaces in attributes;
  numeric CR/LF/tab references retain their values.
- Declared general entities remain literal in both text and attributes.
  Undeclared entities fail with error 11 without a skipped external subset.
  With an external DOCTYPE they reach the default handler in text and are
  omitted in attributes. `standalone="yes"` restores the error.
- DTD attribute defaults still apply with GE disabled. Tokenized attributes
  collapse whitespace; explicit attributes precede appended defaults.
- Qualified names retain prefixes and case. Empty-element end events point
  immediately after the closing `/>`.
- UTF-8, UTF-16LE/BE, Latin-1 and US-ASCII are recognized. A non-ASCII byte in
  US-ASCII fails with error 4. UTF-16 declared on UTF-8 input fails with error
  19; unknown encodings fail with error 18. Latin-1 interprets individual bytes,
  including when those bytes originally encode UTF-8.
- The 3,208-byte long-token case succeeds at every split. This establishes
  behavior above 1,024 bytes, not a maximum token length or a memory budget.

The checked-in outputs characterize Expat only. They are not proof of a Rust
replacement, consumer cache parity, firmware size, or on-device resource use.

The Rust replay checks every non-stop schedule: semantic events, original byte
and line positions, and the final error or completion location. Callback-stop
fixtures remain characterization data: the pull API latches `Stopped`, but does
not emulate Expat's callback fragmentation or parsing-status flags. None of the
five migrated consumers stops inside a callback. Resolve that distinction before
migrating consumers that do, including KOReader XPath resolution.

## Consumer cache baseline

`XmlConsumerOracle` links the production container, OPF, nav, NCX and encryption
consumers, real `BookMetadataCache` serialization and real `FsHelpers`/UTF-8
helpers. Host adapters replace storage and ZIP entry-size lookup. Python
extracts the archive; the C++ tool reads the original XML files and writes
`book.bin`, temporary spine/TOC/item-store bytes and a container/protection summary.
`consumer_goldens/` records those bytes for all 18 repository EPUBs, with both
nav-first and NCX-first selection, each verified at 1/7/512/1024-byte feeding.
The item-store snapshot was recorded before migrating OPF to Rust; earlier
cache expectations were retained. The 1,025-byte CDATA payload crosses both
512- and 1,024-byte boundaries without retaining the original oversized stop
fixture. The complete original fixture set is archived in the round evidence.

```sh
python3 test/xml_oracle/consumers.py --oracle build/xml-oracle/XmlConsumerOracle verify
```

Use `--epub PATH --goldens DIRECTORY` for private harness books, keeping their
outputs outside the repository. This is a consumer/serialization baseline; it
does not invoke the full `Epub` loader, image cover fallback or firmware HAL.
