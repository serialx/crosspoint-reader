# XPath compatibility oracle

`goldens.json.gz` records the vendored Expat resolver's public outputs before the
Rust driver migration. The archive is deterministic gzip JSON; it contains input
SHA-256 values, spine indexes, feed sizes, queries, and exact returned strings.
Inputs are extracted from the existing C++ test literals, additional cases in
`oracle.py`, and every XHTML spine member of every EPUB in `test/epubs`.
No private book data is included.

The deterministic queries cover paragraph indexes (including zero and missing
indexes), visible offsets near the start and feed boundaries and at fractions of
input byte length, and progress values below zero through above one. Small cases
also use 1/7/512-byte feeds; production feeds are 1024 bytes. Results are checked
per feed schedule: whitespace anchoring can depend on character callback splits.
The cases include hidden nesting, comments, PI, CDATA, Unicode, namespaced/case
sensitive elements, declared entities, and a match before malformed trailing XML.

```sh
cmake -S test -B build/test
cmake --build build/test --target XPathOracle KOReaderXPathResolverTest
ctest --test-dir build/test -R 'KOReaderXPathResolver|XPathGoldens' --output-on-failure
```

Only use `oracle.py --oracle PATH record` against the pre-port C++ implementation.
Verification must not regenerate expectations. The host Epub adapter streams
extracted XHTML through the public resolver; it does not exercise ZIP or storage.
