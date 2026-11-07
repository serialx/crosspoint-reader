# Parent output hashes

`parent-results.txt.gz` records 4,490 outputs from the unchanged native parser at
516386af. Each row pins its synthetic input hash, output length and SHA-256, and
physical operation count. `provenance.json` pins the actual capture sources.
`run.py` compares the dormant/active resource owner against these immutable
expectations; ordinary tests never regenerate them. No allocation-profile value
is part of an expected output.

The current capture covers XTC/XTCH, metadata lengths 0/15/16/22/23/29/30/31/63/127,
chapter names up to the format's 80-byte field, legacy headers, short headers,
partial payloads, bad container magic, lazy chapters, page/stream byte results,
close/reopen, and every physical-operation failure ordinal on these routes.
Every parent/candidate pair matched before freezing. The old native class is not
needed by the checker after the active switch; cleanup removes that dependency.

CP0 source snapshots remain evidence-only `.txt` files under
`E/p18-x1/cp0/native-oracle`, with their integrity consumer and retained byte count
in `NATIVE-FREEZE.json`. Neither those snapshots nor this directory is a compiler
include directory. Remove this golden only when no XTC resource test consumes it.
