# Display trace changes for SDK 2.0.0

These fragments are from the upstream FreeInk C++ SDK at
`5e94fe7480f019c89fbf691aef87523eb1b46704`. Build the trace runners with
`-DDISPLAY_TRACE_CPP_SDK=/path/to/exported/sdk`, using the same scenarios, profiles,
buffer options and frame fixture as the historical traces. Each index counts
from zero and includes any preamble before the first step. Each row pins the
historical step's SHA-256 and replaces only its differing middle. The
prefix and suffix line counts preserve identical surrounding bytes.

| Historical cases | Changed steps | Upstream source and effect |
| --- | --- | --- |
| UC8253 facade, single and dual buffers | Second Overlay and both Absolute gray bases | `Uc8253X3Driver::displayGray` remembers gray on the glass through RAM-only cleanup. `grayBaseRefresh` uses a Half refresh followed by its PreBwMid settle when starting from gray. |
| UC8253 direct driver, variants 0–3, single and dual buffers | Factory Full after strip cleanup | The same `grayBaseRefresh` guard honors the requested Full fallback, including its Full and follow-up Fast updates before the settle. Variant 1 exercises a third factory step. |
| UC8279 800×480 on X4, X4 Pro and X4 Classic, probes 02/03/67/68 | First aligned window request | `Uc8279X4Driver::displayWindow` accepts `(16,17,88,33)` and sends PTL `00 10 00 67 00 89 00 a9 01` with the 120-gate offset. The unaligned and inverted steps retain their historical bytes. |
| EEGO A4 facade and direct driver, with all allocation cases | Rail setup, Full and Half, AA uploads and cleanup, Gray3 and wake | `FreeInkDisplay` drops GPIO6 hold before driving the rail. `Uc8279cA4Driver` applies upstream polarity, Full cadence, post-gray refresh and retained baselines. |
| Sticky and Picco SSD2677 facade and retained driver, with all allocation cases | Reset, latch/power waits and refresh power sequencing | `Ssd2677Driver` uses upstream ready waits and Picco refresh power shutdown. |
