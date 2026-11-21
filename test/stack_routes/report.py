"""Check budgets against the integrated baseline and print the protected X3 routes."""

import json
import sys
from collections import Counter
from pathlib import Path

out = Path(sys.argv[1])
here = Path(__file__).resolve().parent
work = out / "stack/candidate/default"


def read(path):
    return json.loads(path.read_bytes())


baseline = read(here / "data/totals.json")
measurement = read(work / "selected-production-chains.json")
identity = measurement["elf_sha256"]
routes = measurement["routes"]
sleep = read(out / "sleep-continuations.json")
assert identity == sleep["elf"]
changes = {}


def gate(name, before, after, ceiling):
    if before != after:
        changes[name] = [before, after]
    assert after <= ceiling and (after <= before or ceiling - after >= 800), (name, before, after, ceiling)


assert routes.keys() == baseline["routes"].keys()
for name, route in routes.items():
    before = baseline["routes"][name]
    task = route["task"]
    if task in ("loop", "render"):
        gate(name, before, route["total"], 8192 if task == "loop" else 6656)
    else:
        assert before == route["total"], (task, name)

assert sleep["rows"].keys() == baseline["sleep"].keys()
for name, rows in sleep["rows"].items():
    assert rows.keys() == baseline["sleep"][name].keys()
    for kind, row in rows.items():
        gate(name + "/" + kind, baseline["sleep"][name][kind], row["total"], 8192)

protected = {}
for name, key, ceiling in [
    ("Dictionary forward", "d1-forward-b1-definition-parser-step-metric-0", 6912),
    ("Dictionary recovery", "d1-recover-b1-definition-parser-step-metric-0", 6960),
    ("Definition header", "o4b-definition-header-theme", 6272),
    ("Bookmark header", "owned-list-header-bookmarks", 6320),
    ("Keyboard truncation", "rust-keyboard-sdk-key-truncate-sd-open", 6304),
    ("Keyboard measurement", "rust-keyboard-sdk-key-measure-sd-open", 6176),
]:
    protected[name] = (routes[key]["total"], ceiling)
protected["Cover + Custom fallback"] = (
    sleep["rows"]["cover -> custom/source open"]["fatal"]["total"],
    6768,
)
book_get = read(work / "file-get-bindings.json")["routes"]
assert len(book_get) == 111
protected["File GET maximum"] = (max(routes[key]["total"] for key in book_get), 8192)

wifi = read(here / "data/wifi.json")
frames = {}
members = {name: [] for name in wifi["definitions"]}
with (work / "resolved-calls.jsonl").open() as stream:
    for line in stream:
        row = json.loads(line)
        name = row["name"]
        frames[name] = max(frames.get(name, 0), row["frame"])
        if name in members:
            members[name].append(
                dict(frame=row["frame"], targets=Counter(call["name"] for call in row["calls"]))
            )
assert members == wifi["definitions"], "Requalify the changed Wi-Fi physical suffix"
for name, key, ceiling in [
    ("Wi-Fi fatal measurement", "wifi_measure", 6144),
    ("Wi-Fi fatal raster", "wifi", 6032),
]:
    protected[name] = (
        sum(frames[n] for n in dict.fromkeys(wifi["prefixes"][key])) + wifi["suffix"],
        ceiling,
    )

metadata = read(out / "metadata/RESULT.json")
prefix = read(out / "SLEEP-STACK.json")["candidate"]
assert metadata["elf_sha256"] == prefix["elf_sha256"] == identity
spec = read(here / "x3-default-sleep-event.json")
for name, chain in spec["routes"].items():
    assert [frame["name"] for frame in prefix["branches"][name]["frames"]] == chain
fatal = metadata["roots"][spec["metadata_continuation"]]["fatal"]["bytes"]
bound = prefix["branches"]["emitter"]["sum"] + fatal
gate("Sleep-entry plugin metadata fatal", 6704, bound, spec["gate"])
protected["Sleep-entry plugin metadata fatal"] = (bound, spec["gate"])

for name, (value, ceiling) in protected.items():
    assert value <= ceiling, (name, value, ceiling)
    print(f"{name:36} {value:5} / {ceiling:5} B")
summary = dict(
    elf=identity,
    reference=baseline["elf"],
    routes=len(routes),
    continuations=sum(map(len, sleep["rows"].values())),
    changes=changes,
    protected=protected,
)
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(
    f"{summary['routes']} routes, {summary['continuations']} sleep continuations, {len(changes)} changed totals"
)
