# Merging a display SDK update

- Record the previous upstream SDK, the incoming upstream SDK, and the firmware's fork gitlink. Compare the two upstream revisions, including paths the fork deleted. The 2.0.0 merge took upstream `5e94fe74` and shipped fork `50ba72b70`.
- Inspect `libs/display/FreeInkDisplay/src/driver/`. Prior conflicts included the SSD1677, SSD2677, UC8253 X3, UC8279 X4 and UC8279C A4 implementation files and the factory headers for those controllers, Paper Mono, UC8179 and UC8279 X3. Port upstream command sequences, LUTs, BUSY waits, gray/OLD plane state, regional writes and between-frame power changes for Rust-backed controllers into `rust/crates/xp-display`. Check board-specific fallback behavior when a previous framebuffer is absent.
- Review buffer ownership and completion in `FreeInkDisplay.cpp`, controller selection in `BoardConfig.h`, the transport in `EpdBus.cpp`, and the interface in `PanelDriver.h` and `RustPanelDriver.cpp`. Map virtual slots before and after any interface change. Check the consumer's port locks and the paired C ABI header and Rust archive on each affected board.
- Refresh the affected recording-bus expectations from the upstream behavior. Prior conflicts included the Pro stubs for Arduino, EpdBus and heap caps, and the Pro, Picco/Sticky, UC8253 and A4 host fixtures and runners. Check the LUT fixtures and single- and dual-buffer Pro cases. From `freeink-sdk`, run the six commands below with the matching firmware checkout. Keep the SDK's `docs/testing.md` in sync with the runners.
- Before publishing a firmware gitlink, verify that the corresponding SDK commit is reachable on its configured remote and that a fresh recursive firmware checkout resolves it.

```sh
for suite in bus pro uc8279 uc8279c_a4 uc8253_power picco
do
  python3 "libs/display/FreeInkDisplay/test/host/run_${suite}.py" || exit 1
done
```
