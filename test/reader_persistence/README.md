# Reader persistence checks

`reader_persistence_order` compiles the shipping Rust progress, storage-read and
time orchestration against strict physical fakes. It checks persisted progress
bytes and events for open, short-write, close, remove and rename failures;
borrowed read lifetime; and clock-floor/sync save ordering.

`reader_store_parity` compiles the shipping Rust recents/state module and the
existing native stores with ArduinoJson. It runs 54 paired sequences: normal
writes and failure at each of 26 serialized write calls, with directory creation
succeeding or failing. Every pair compares file bytes, row/state contents and
ordered storage/log events after each operation. The workload includes pruning,
reordering, the ten-book limit, missing removal, close state, and finished-book
path and cover replacement. Native obfuscation fails the test if reached.

The reader lifecycle suite retains its historical `app-test` recents/time
facades for its existing scenario oracles. These tests separately exercise the
shipping one-way store primitives and direct Rust service calls.

Run both through CTest, or run the progress/time tests with
`cargo test --offline --manifest-path test/reader_persistence/Cargo.toml`.
The store runner accepts the repository's configured ArduinoJson include path;
CTest supplies it from `FETCHCONTENT_SOURCE_DIR_ARDUINOJSON`.
