# Keyboard behavior oracle

`record.py` compiles the production keyboard state methods, URL tables,
layout-set policy and ButtonNavigator against the pinned SDK's real layouts.
Only Activity, input, time and result delivery are replaced by host leaves.
Text-point placement and rendering are excluded from this transcript runner.
`widgets.py` records the real SDK keyboard drawing operations and touch areas
with fixed host font metrics at four widths, ten languages, four pages and
button/touch configurations, plus the application's three URL layouts.
The emulator screen oracle supplies real-font pixel and caller coverage.

The split gzip transcript is one byte stream. It covers all 65,536 layout masks,
all layout keys and alternate outputs, four byte limits, UTF-8 editing, masking,
page cycling, cursor navigation, long-press thresholds and completion/cancel.
`test_oracle.py` also changes the production cursor increment to zero and checks
that the frozen transcript rejects it. The goldens were captured at ba6860d8
with SDK 61c49287 before the port.

Run `python3 test/keyboard_oracle/test_oracle.py`. For an archived source tree,
use `record.py --root PATH --out TRANSCRIPT`. Goldens contain synthetic text only.
