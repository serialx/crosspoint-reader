# Text settings oracle

`Oracle.cpp` freezes the production Text Settings layout and shared tab/list
geometry at `dc21f50f`, using the pinned SDK widgets and real built-in metrics.
`layout.golden` records 353 cases across board profiles, four orientations,
Lyra/RoundedRaff metrics, tab focus, checked rows and partial trailing rows.
The runner also verifies that changing a tab inset by one pixel is detected.

`NavigationOracle.cpp` freezes tab selection and hold behavior at the same
revision. Its 300 records cover all four initial tabs, five ring positions,
press/release, repeat boundaries, confirmation and cancellation.

Run `python3 test/text_settings/run.py`. The oracle has no firmware effect.
