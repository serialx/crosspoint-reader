# Network UI tests

The Rust Wi-Fi owner tests exercise input, effects, resource lifetime and failure
recovery with deterministic physical leaves. The FlowText tests check every
compiled language, maximum-length SSIDs, mutable loans of both existing header
buffers, header restoration on repaint and absence of owner allocations during
rendering. Oversized persisted SSIDs are also exercised through automatic
connection success and failure; those paths do not enter Save or Forget prompts.

`NetworkWifiFlowTarget` and `NetworkWifiX3FlowTarget` compare the production
FlowText adapter with the frozen SDK dialog renderer. They cover Save and Forget,
all four orientations, button/touch layouts, styles and selection, and bounded
ASCII/mixed UTF-8 SSIDs up to 32 bytes. The adapter keeps the SDK's emitted bytes,
geometry, wrapping and ellipsis; it copies the emitted lines into owner storage
and returns from layout before painting them. The Rust owner tests separately
check the storage loan and all translated title/prefix lengths.

The older facade/legacy comparisons retain their native predecessor and its
shared binding frozen at `b69dc74b`. Their synthetic SSIDs reach 255 bytes and
test the historical general SDK contract. They do not establish that a current
Wi-Fi prompt can receive such an SSID. `prepare_wifi_parent.py` verifies the
frozen source hashes; none of these tests regenerate pixel goldens.

Run the affected targets through CMake/CTest and the owner tests with
`cargo test --manifest-path rust/Cargo.toml -p xp-ui --lib network::wifi_owner`.
Emulator fixtures and capture coverage are tracked separately in
[`test/ui_screens`](../ui_screens/README.md).
