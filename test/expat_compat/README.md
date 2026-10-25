# SDK XML compatibility oracle

The baseline captures the pinned SDK's unmodified `OpdsFeedParser`,
`OpenSearchDescParser` and `XmlListParser` against firmware Expat 2.7.3 with
`XML_GE=0` and `XML_CONTEXT_BYTES=0`, before adding the Rust compatibility
adapter. Thirty inputs run under six write/read schedules (180 cases).

`ConsumerOracle` emits all public feed metadata, every entry/facet field,
OpenSearch templates, catalog rows, truncation and final error status. Strings
are hex encoded without normalization. Inputs include Atom namespace prefixes,
entities and DTD defaults, CDATA, UTF-16, malformed and truncated XML, long
fields, entry caps, WebDAV URLs and the OPDS 2 format-sniffing path. The real
SDK JSON backend is linked so the feed wrapper is not replaced by a stub.

```sh
cmake -S test -B build/test
cmake --build build/test --target ExpatConsumerBaseline ExpatConsumerAdapter ExpatAdapterTest ExpatSdkCatalogTest -j 6
ctest --test-dir build/test -R '^Expat' --output-on-failure
```

`oracle.py --capture` refuses to overwrite the compressed baseline. Do not
regenerate it to accept an adapter mismatch. This oracle covers parsed public
results and errors, not HTTP transport, network activity or GUI behavior.

The baseline uses `test/vendor/expat`; the adapter uses `lib/expat` and the
shared Rust host archive. `api-goldens.json.gz` additionally compares the
active SDK diagnostic surface: parse status, numeric error, line number and
error string. It does not assert byte-index diagnostics (unused by these SDK
consumers and not implemented by the adapter). The existing SDK CatalogList
host tests run unchanged against the adapter. Five focused adapter tests cover
allocation failure, cancellation, long buffered writes and bounds checking.
