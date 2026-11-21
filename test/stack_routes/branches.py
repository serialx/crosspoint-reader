"""Paired X3 sleep selections. Real calls/tails, callback-table witnesses, and physical-tail closures."""

import hashlib
import json
import re
import struct
import sys
from pathlib import Path

from graph_inventory import graph

work, physical, out = map(Path, sys.argv[1:])
f, g, indirect, body = graph(work)
phys = json.loads(physical.read_text())
rows = {}
ev = {}
dyn = {}
site_filter = {}


def match(s):
    if s in f:
        return s
    hits = [n for n in f if s in n and "non-virtual thunk" not in n]
    assert len(hits) == 1, (s, hits)
    return hits[0]


def maybe(s):
    return next((n for n in f if n.startswith(s)), None)


def oneprefix(prefix):
    return match(prefix)


symbols = (work / "symbols.txt").read_text().splitlines()
elf = (work / "firmware.elf").read_bytes()
assert hashlib.sha256(elf).hexdigest() == phys["elf_sha256"]
phoff = struct.unpack_from("<I", elf, 28)[0]
size, count = struct.unpack_from("<HH", elf, 42)
segments = [struct.unpack_from("<8I", elf, phoff + i * size) for i in range(count)]


def ptr(symbol, offset):
    (a,) = [int(s.split()[0], 16) + offset for s in symbols if s.endswith(" " + symbol)]
    for kind, i, v, _, length, *_ in segments:
        if kind == 1 and v <= a <= v + length - 4:
            target = struct.unpack_from("<I", elf, i + a - v)[0]
            return next(n for n, r in f.items() if int(r["address"], 16) == target)
    raise ValueError(symbol)


sleep = ptr("(anonymous namespace)::appRuntime()::runtime", 0x64)
legacy = ptr("ActivityManager::begin(XpAppRuntime const&)::legacy", 8)
on = ptr("vtable for SleepActivity", 20)
enter = match("Services<xp_ffi::app::Owner>>::enter")


def binding(a, b, why):
    assert indirect[a] or re.search(r"\bjr\s+a[0-7]", body[a]), a
    dyn[a, b] = why


binding("xp_app_loop", sleep, "Runtime.sleep_enter +0x64")
binding(enter, legacy, "legacy enter +8")
binding(legacy, on, "Sleep vtable +12 after address point")
entry = [
    "loopTask(void*)",
    "loop()",
    "ActivityManager::loop()",
    "xp_app_loop",
    sleep,
    "enterDeepSleep(bool)",
    "ActivityManager::goToSleep(bool)",
    match("::send(XpAppEvent const&)"),
    "xp_app_event",
    match(">>::drain::<xp_ffi::app::App>"),
    match(">>::install::<xp_ffi::app::App>"),
    enter,
    legacy,
    on,
    match("SleepActivity::renderSleepRoute(unsigned char)"),
]
# Qualified dispatch prefixes use each caller's nominal frame. A tail call that has already
# restored SP consumes no frame; use its DWARF CFA at that instruction.
cfa = {}
for block in (work / "frames-interp.txt").read_text().split("\n\n"):
    m = re.search(r" pc=([0-9a-f]+)\.\.", block)
    if m:
        cfa[int(m[1], 16)] = [
            (int(a, 16), int(n)) for a, n in re.findall(r"^([0-9a-f]{8})\s+sp\+(\d+)\b", block, re.M)
        ]


def indirect_proof(a, b):
    lines = body[a].splitlines()
    allpcs = {c["pc"] for c in indirect[a]}
    sites = []
    for i, line in enumerate(lines):
        pc = line.split(":")[0]
        if pc not in allpcs and not re.search(r"\bjr\s+a[0-7]\s*$", line):
            continue
        reg = line.split()[-1]
        window = lines[max(0, i - 10) : i]
        if (a, b) in site_filter and not site_filter[a, b](reg, window):
            continue
        if a == "xp_store_load" and pc != sorted(allpcs)[0]:
            continue
        slot = (
            100
            if a == "xp_app_loop"
            else 8
            if a == enter
            else 12
            if a == legacy
            else 12
            if b == "HalFile::write(unsigned char const*, unsigned int)"
            else 8
            if b == "HalFile::write(unsigned char)"
            else None
        )
        if slot is not None:
            writes = [l for l in window if re.search(r"\b(?:lw|li|mv|addi|lui|add)\s+" + reg + r",", l)]
            if not writes or not re.search(r"\blw\s+" + reg + "," + str(slot) + r"\(", writes[-1]):
                continue
        sites.append(dict(pc=pc, instructions=window + [line]))
    assert sites, (a, b)
    return dict(target=f[b]["address"], callee_frame=f[b]["frame"], sites=sites)


def witness(a, b):
    key = a + " -> " + b
    if key in ev:
        return ev[key]["frame"]
    nominal = f[a]["frame"]
    direct = [c["pc"] for c in f[a]["calls"] if c["name"] == b]
    tail = [l for l in body[a].splitlines() if re.search(r"\b(?:j|jr)\s", l) and "<" + b + ">" in l]
    assert direct or tail or (a, b) in dyn, (a, b)
    frame = nominal
    if tail and not direct and (a, b) not in dyn and a not in entry[:-1]:

        def live(line):
            pc = int(line.split(":")[0], 16)
            known = [v for addr, v in cfa.get(int(f[a]["address"], 16), []) if addr <= pc]
            return known[-1] if known else nominal

        frame = max(map(live, tail))
    ev[key] = dict(
        frame=frame,
        direct=direct,
        tail=tail,
        callee=f[b]["address"],
        callee_frame=f[b]["frame"],
        binding=dyn.get((a, b)),
    )
    if (a, b) in dyn:
        ev[key]["indirect"] = indirect_proof(a, b)
    return frame


def report(label, chain):
    chain = [n for n in chain if n]
    prefix = sum(witness(a, b) for a, b in zip(chain, chain[1:]))
    last = chain[-1]
    tails = phys["roots"].get(last, {"entry": {"bytes": f[last]["frame"]}})
    rows[label] = {
        k: dict(total=prefix + v["bytes"], margin=8192 - prefix - v["bytes"], root=last, chain=chain)
        for k, v in tails.items()
        if v["bytes"] >= 0
    }


def paths(a, b, via=()):
    allowed = set(via) | {b}

    def recurse(n, seen):
        if n == b:
            return [n]
        for t in sorted((g[n] | {y for x, y in dyn if x == n}) & allowed - set(seen)):
            p = recurse(t, seen + [t])
            if p:
                return [n] + p
        return []

    path = recurse(a, [a])
    assert path, (a, b, via)
    return path[1:]


def connect(chain, end, via=()):
    return chain + paths(chain[-1], end, via)


native = lambda name: match("SleepActivity::" + name + "(")
custom = native("renderCustomSleepScreen")
overlay = native("renderTransparentCustomSleepScreen")
cover = "SleepActivity::renderCoverSleepScreen()"
clone = maybe(cover + " [clone")
bmap = native("renderBitmapSleepScreen")
default = native("renderDefaultSleepScreen")
for tag, method in [
    ("default", default),
    ("blank", native("renderBlankSleepScreen")),
    ("quick", native("renderLastScreenSleepScreen")),
]:
    report(tag + "/display", entry + [method])
coverDispatch = entry + [cover]
book = coverDispatch + ([clone] if clone else [])
bookfile = maybe("SleepActivity::renderBookCoverFile(")
coverdraw = connect(
    coverDispatch, bmap, [n for n in [clone, bookfile, maybe("SleepActivity::renderBmpFile(")] if n]
)
# Failed XTC/EPUB attempt: the base calls back from the cover body, candidate returns to route switch.
old = bool(clone and entry[-1] in g[clone])
fallback = book + [entry[-1]] if old else entry
customBmp = maybe("SleepActivity::renderCustomSleepBmp(")
select = match("::selectRandomSleepFile(")
scan = match("::findNextValidSleepImage(")
read = "HalStorage::openFileForRead(char const*, char const*, HalFile&)"
write = read.replace("ForRead", "ForWrite")
readOver = match("HalStorage::openFileForRead(char const*, std::__cxx11")
overlayPath = native("renderSleepOverlayPath")
overlayFile = "SleepActivity::renderSleepOverlayFile(HalFile&, char const*)"
overlayClone = maybe(overlayFile + " [clone")
png = native("renderTransparentOverlayPng")
draw = match("GfxRenderer::drawBitmap(Bitmap const&")
readrow = match("Bitmap::readNextRow(unsigned char*, unsigned char*) const")
get = match("PersistableStoreBase::readDocFromFile(")
put = match("PersistableStoreBase::writeDocToFile(")
png_file_read = match("(anonymous namespace)::readFile(void*, unsigned long, unsigned char*, unsigned int)")
png_read_name = "(anonymous namespace)::read(void*, unsigned long, unsigned char*, unsigned int)"
png_thunk = "PNG Context input callback from PngToFramebufferConverter.cpp"
png_bodies = re.split(r"(?=[0-9a-f]{8} <\(anonymous namespace\)::read\(void\*)", body[png_read_name])
(png_thunk_body,) = [
    item for item in png_bodies if re.search(r"\bj\s+\w+ <" + re.escape(png_file_read) + ">", item)
]
png_thunk_address = png_thunk_body.split()[0]
(png_thunk_symbol,) = [
    r
    for line in (work / "resolved-calls.jsonl").open()
    if (r := json.loads(line))["address"] == png_thunk_address
]
assert png_thunk_symbol["name"] == png_read_name and png_thunk_symbol["frame"] == 0
f[png_thunk] = png_thunk_symbol
g[png_thunk] = {png_file_read}
body[png_thunk] = png_thunk_body
png_info = match("xp_core::png::info::<xp_ffi::png::Services>")
png_input = match("<xp_core::png::Decoder>::input::<xp_ffi::png::Services>")
for caller in (png_info, png_input):
    binding(
        caller,
        png_thunk,
        "XpPngServices.read +4, the Context read thunk installed by PngToFramebufferConverter::decodeToFramebuffer",
    )
    site_filter[caller, png_thunk] = lambda reg, lines: any(
        re.search(r"\blw\s+" + reg + r",4\(\w+\)", line) for line in lines
    )
binding(
    "xp_png_dimensions",
    png_file_read,
    "xp_png_dimensions second native argument: readFile, preserved in s0",
)
site_filter["xp_png_dimensions", png_file_read] = lambda reg, lines: reg == "s0"
decode_step = match(
    "<xp_core::png::Decoder>::decode::<xp_ffi::png::Services, xp_ffi::png::xp_png_render::{closure#0}>"
)
png_refill = match("<xp_core::png::Decoder>::refill::<xp_ffi::png::Services>")
png_byte = match("<xp_core::png::Decoder>::byte::<xp_ffi::png::Services>")
png_stream = [decode_step, png_refill, png_byte, png_input, png_thunk, png_file_read]
ops = match("::DocumentIo::ops()::{lambda")
binding("xp_store_document", ops, "DocumentIo::ops single persist callback for op 0..5")
bootstrap = match("PersistableStore<CrossPointState>::loadFromFile()::{lambda(void*)#1}")
binding(
    "xp_store_load",
    bootstrap,
    "setup instantiates PersistableStore<CrossPointState>::loadFromFile load callback",
)
boot = ["loopTask(void*)", "setup()", "xp_store_load", bootstrap]
for label, r in [
    ("exists", "HalStorage::exists(char const*)"),
    ("read", "HalStorage::readFile(char const*)"),
]:
    report("boot/shuffle state " + label, boot + [get, "xp_store_document", ops, r])
report("boot/decode state fields", boot + [match("CrossPointState::fromJson("), "xp_store_state_load"])
rootnum = match("<xp_ffi::settings::JsonOps>::root_num")
rootput = match("<xp_ffi::settings::JsonOps>::root_put")
jsonget = match(
    "store_bridge::JsonInput::ops()::{lambda(void*, XpStoreJsonPath const*, unsigned long, unsigned long long)#1}"
)
jsonput = match(
    "store_bridge::Json::ops()::{lambda(void*, XpStoreJsonPath const*, unsigned long, unsigned long long, XpStoreBytes)"
)
binding(rootnum, jsonget, "root_num -> JsonInput::ops.query for U32 / prior scalar fields")
binding(rootput, jsonput, "root_put -> Json::ops.put for numbers")
load = boot + [match("CrossPointState::fromJson("), "xp_store_state_load"]
report("boot/JSON scalar or missing field", load + [rootnum, jsonget])
parse = next(n for n in g[ops] if "::parseVariant<" in n)
report("boot/parse state document", boot + [get, "xp_store_document", ops, parse])
serialize = next(n for n in g[ops] if "::JsonSerializer<" in n)
log = "logPrintf(char const*, char const*, char const*, ...)"
save = match("PersistableStore<CrossPointState>::saveToFile() const")
pre = entry[: entry.index("enterDeepSleep(bool)") + 1] + [save]
sf = pre + [match("CrossPointState::toJson("), "xp_store_state_save"]
report("pre-dispatch/state fields", sf)
report("pre-dispatch/JSON scalar write", sf + [rootput, jsonput])
store = pre + [put, "xp_store_document", ops]
report("pre-dispatch/serialize flat state document", store + [serialize])
for op in ["HalStorage::mkdir(char const*, bool)", "HalStorage::writeFile(char const*, String const&)"]:
    report("pre-dispatch/state save " + op, store + [op])
for tag, chain in [("cover", book)]:
    for s in [
        "Xtc::load()",
        "Xtc::generateCoverBmp() const",
        "Epub::load(bool, bool)",
        "Epub::generateCoverBmp(bool, bool) const",
    ]:
        report(
            tag + "/" + s,
            connect(
                coverDispatch, s, [n for n in [clone, maybe("(anonymous namespace)::findBookCover(")] if n]
            ),
        )
    report(
        "cover/open rendered book",
        connect(coverDispatch, read, [v for v in [clone, bookfile, readOver] if v]),
    )
    report(
        "cover/parse header",
        connect(coverDispatch, "Bitmap::parseHeaders()", [v for v in [clone, bookfile] if v]),
    )
    report("cover/image rows", coverdraw + [draw, readrow, "HalFile::read(void*, unsigned int)"])
    report("cover/missing default", fallback + [default])


def selector(label, chain):
    for target in [
        "HalStorage::open(char const*, int)",
        "HalFile::openNextFile()",
        "HalFile::getName(char*, unsigned int)",
        "Bitmap::parseHeaders()",
        "HalFile::read(void*, unsigned int)",
    ]:
        report(label + "/scan " + target, connect(chain + [select], target, [scan]))
    report(
        label + "/state fields", chain + [select, match("CrossPointState::toJson("), "xp_store_state_save"]
    )
    sf = chain + [select, match("CrossPointState::toJson("), "xp_store_state_save"]
    report(label + "/JSON scalar write", sf + [rootput, jsonput])
    report(
        label + "/serialize flat state document", chain + [select, put, "xp_store_document", ops, serialize]
    )
    for op in ["HalStorage::mkdir(char const*, bool)", "HalStorage::writeFile(char const*, String const&)"]:
        report(label + "/state save " + op, chain + [select, put, "xp_store_document", ops, op])
    cycle = "SleepImageCycle::next(unsigned short)"
    if cycle in f:
        report(label + "/shuffle next", chain + [select, cycle])
    report(label + "/no selection", chain + [default])


for lbl, prefix in [("custom", entry), ("cover -> custom", fallback)]:
    chain = prefix + [custom]
    selector(lbl, chain)
    c = chain + ([customBmp] if customBmp else [])
    report(lbl + "/source open", connect(c, read, [readOver]))
    helpers = [
        n
        for n in f
        if n.startswith(("SleepActivity::renderScreenReadyBmp(", "SleepActivity::renderBmpFile("))
    ]
    for n in helpers or [None]:
        h = c + ([n] if n else [])
        report(lbl + "/parse " + (n or "source"), h + ["Bitmap::parseHeaders()"])
        extent = "Bitmap::hasCompletePixelData() const"
        if extent in g[h[-1]]:
            report(lbl + "/cached pixel extent", h + [extent, "HalFile::fileSize64()"])
        report(
            lbl + "/image rows " + (n or "source"),
            h + [bmap, draw, readrow, "HalFile::read(void*, unsigned int)"],
        )
    if customBmp:
        key = maybe("SleepCoverCache::makePaths(")
        if key or "snprintf" in g[customBmp]:
            kpath = c + ([key] if key else [])
            for target in ["snprintf", "std::_Hash_bytes(void const*, unsigned int, unsigned int)"] + (
                ["operator new[](unsigned int, std::nothrow_t const&)"]
                if "operator new[](unsigned int, std::nothrow_t const&)" in g[kpath[-1]]
                else []
            ):
                report(
                    lbl + "/cache key " + target,
                    connect(
                        kpath, target, [n for n in f if n.startswith("std::hash<std::basic_string_view")]
                    ),
                )
            for target in ["HalFile::modificationTime()", "HalFile::size()"]:
                report(lbl + "/cache source " + target, c + [target])
        for target in [
            read,
            write,
            "HalStorage::exists(char const*)",
            "HalStorage::rename(char const*, char const*)",
            "HalStorage::remove(char const*)",
            "HalFile::close()",
        ]:
            report(
                lbl + "/cache " + target,
                connect(
                    c, target, [readOver, match("HalStorage::openFileForWrite(char const*, std::__cxx11")]
                ),
            )
        conv = next(
            n for n in f if n.startswith("BmpToBmpConverter::bmpFileToBmpStream") and "lambda" not in n
        )
        cc = c + [conv]
        if "Bitmap::hasCompletePixelData() const" in g[conv]:
            report(
                lbl + "/converter pixel extent",
                cc + ["Bitmap::hasCompletePixelData() const", "HalFile::fileSize64()"],
            )
        row = match("Bitmap::readRowGray(")
        via = [n for n in f if n.startswith(conv + "::{lambda")]
        for t in [
            (
                "HalFile::seek64(unsigned long long)"
                if "HalFile::seek64(unsigned long long)" in g[row]
                else "HalFile::seek(unsigned int)"
            ),
            "HalFile::read(void*, unsigned int)",
        ]:
            report(lbl + "/convert " + t, connect(cc, row, via) + [t])
        report(lbl + "/conversion failure", cc + [log])
        if via:
            report(lbl + "/row failure", cc + [via[0], log])
        for helper, target in [
            (match("::writeLE(Print&"), "HalFile::write(unsigned char)"),
            (None, "HalFile::write(unsigned char const*, unsigned int)"),
        ]:
            root = cc + ([helper] if helper else [])
            binding(
                root[-1],
                target,
                "converter output is HalFile; single-byte or buffer Print virtual overload",
            )
            report(lbl + "/convert " + target, root + [target])
for lbl, start in [
    ("overlay", entry + [overlay]),
    ("cover fails -> overlay", fallback + [overlay]),
    ("cover drawn + overlay", coverdraw + [overlay]),
]:
    selector(lbl, start)
    path = start + [overlayPath]
    report(lbl + "/root or selected BMP open", connect(path, read, [readOver]))
    report(lbl + "/PNG exists", path + ["HalStorage::exists(char const*)"])
    report(lbl + "/PNG dimensions", path + [png, match("PngToFramebufferConverter::getDimensionsStatic(")])
    report(lbl + "/PNG decode", path + [png, match("PngToFramebufferConverter::decodeToFramebuffer(")])
    alpha = path + [overlayFile]
    report(
        lbl + "/alpha row",
        alpha + [match("::renderTransparentOverlayPass("), "HalFile::read(void*, unsigned int)"],
    )
    regular = alpha + ([overlayClone] if overlayClone else [])
    report(lbl + "/regular BMP row", regular + [bmap, draw, readrow, "HalFile::read(void*, unsigned int)"])
    if lbl == "cover drawn + overlay":
        report(
            lbl + "/plain book if overlay missing",
            start + [bmap, draw, readrow, "HalFile::read(void*, unsigned int)"],
        )
        gray = match("(anonymous namespace)::beginOverlayGrayPlane(")
        for mode, gray_prefix in [
            ("PNG", path + [png, gray]),
            ("alpha BMP", alpha + [gray]),
            ("regular BMP", regular + [bmap, gray]),
        ]:
            label = lbl + "/gray cover " + mode + " LSB and MSB"
            rewind = "Bitmap::rewindToData() const"
            report(label + " rewind", gray_prefix + [rewind, "HalFile::seek(unsigned int)"])
            report(
                label + " image rows", gray_prefix + [draw, readrow, "HalFile::read(void*, unsigned int)"]
            )
# Report the branch after a failed leaf as well as the successful row I/O.
# Each selected log format in these rows uses integers, strings or literal text.
for label, chain in [(k, next(iter(v.values()))["chain"]) for k, v in list(rows.items())]:
    if "/scan HalFile::openNextFile()" in label:
        cursor = chain[:-1]
        if log in g[cursor[-1]]:
            report(label.split("/scan ")[0] + "/scan skipped or invalid entry log", cursor + [log])
    if "/state save HalStorage::writeFile(" in label:
        w = chain[: chain.index(put) + 1]
        report(label.split("/state save ")[0] + "/state write failure log", w + [log])
    if label.endswith("/source open"):
        origin = label.rsplit("/", 1)[0]
        worker = chain[: chain.index(customBmp) + 1] if customBmp else chain[: chain.index(custom) + 1]
        if log in g[worker[-1]]:
            report(origin + "/source or cache failure log", worker + [log])
    if draw in chain and readrow in chain:
        prefix = chain[: chain.index(draw) + 1]
        if log in g[draw]:
            report(label + " draw failure log", prefix + [log])
        mono = match("GfxRenderer::drawBitmap1Bit(")
        report(label + " 1-bit row", prefix + [mono, readrow, "HalFile::read(void*, unsigned int)"])
        report(label + " 1-bit allocation or read failure log", prefix + [mono, log])
    if label.endswith("/PNG dimensions") or label.endswith("/PNG decode"):
        report(label + " input open", connect(chain, read, [readOver]))
        if log in g[chain[-1]]:
            report(label + " error log", chain + [log])
        if label.endswith("/PNG dimensions"):
            stages = {"header": chain + ["xp_png_dimensions", png_file_read]}
        else:
            render = chain + ["xp_png_render"]
            stages = {"info": render + [png_info, png_thunk, png_file_read], "IDAT": render + png_stream}
        for stage, prefix in stages.items():
            for action in [
                "HalFile::position() const",
                "HalFile::seek(unsigned int)",
                "HalFile::read(void*, unsigned int)",
            ]:
                report(label + " " + stage + " -> " + action, prefix + [action])
    if label.endswith("/root or selected BMP open"):
        screen = chain[: chain.index(overlay) + 1]
        if log in g[overlay]:
            report(label.split("/root or")[0] + "/overlay unavailable", screen + [log])
    if label.endswith("/alpha row"):
        af = chain[: chain.index(overlayFile) + 1]
        if log in g[overlayFile]:
            report(label + " header or buffer failure log", af + [log])
    if label.endswith("/regular BMP row"):
        rf = chain[: chain.index(bmap)]
        if log in g[rf[-1]]:
            report(label + " invalid header log", rf + [log])
    if label == "cover/Xtc::load()":
        report("cover/XTC load error log", chain + [log])
        if log in g[chain[-2]]:
            report("cover/book unavailable or generation failed log", chain[:-1] + [log])
    if chain[-1] == "Bitmap::parseHeaders()":
        parser = chain[-1]
        for target in sorted(g[parser] & phys["roots"].keys()):
            report(label + " -> " + target, chain + [target])
        for helper in sorted(n for n in g[parser] if n.startswith("Bitmap::readLE")):
            for target in sorted(g[helper] & phys["roots"].keys()):
                report(label + " -> " + helper + " -> " + target, chain + [helper, target])
out.write_text(json.dumps(dict(elf=phys["elf_sha256"], rows=rows, edges=ev), indent=2))
print(len(rows), "branches")
