# Shipping input composition

`runtime_input_native` extracts the production HAL poll, mapped-query methods
and reader service callbacks. Deterministic hardware stubs check raw poll versus
edge-update ordering, explicit Home actions, remote short circuits, long-press
release suppression and the raw tilt-state return. Native compatibility still
uses the same phases and queries.

`runtime_input_shipping` compiles the production Rust input adapters and HAL
policy without `app-test`. It checks one poll, edge update before feedback,
`input_polled` before long-press reset, the following mapped snapshot, deferred
Home delivery without replaying feedback, live queries through the cached table
address, and one-time tilt consumption. Its reduced App/ABI and hardware leaves
are test doubles; complete firmware ABI/layout and native bindings are separate
build checks. These tests do not measure physical debounce timing, SDK thread
behavior, or other boards' haptic continuations.

Run both through CTest, or run `python3 test/runtime_input/native.py` and
`cargo test --offline --manifest-path test/runtime_input/Cargo.toml`.
