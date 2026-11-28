"""Physical XTC HAL/log/allocator continuation bounds on an ordinary qualified C3 ELF."""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from graph_inventory import graph, reachable, scc

p = argparse.ArgumentParser()
p.add_argument("--metadata", action="store_true")
p.add_argument("--image", type=Path, required=True)
p.add_argument("--fatal", type=Path, required=True)
p.add_argument("--out", type=Path, required=True)
a = p.parse_args()
a.out.mkdir(exist_ok=True)
f, g, u, b = graph(a.image)
fatal = json.loads(a.fatal.read_text())
notes = []
typed = []
throw = set(fatal["roots"]) | {"std::terminate()", "__assert_func", "__stack_chk_fail"}
caps = {
    x["name"]: x["bound"]
    for x in fatal["functions"]
    if x["name"] not in throw and x["name"] != "operator new(unsigned int)"
}
roots = [
    "HalStorage::openFileForRead(char const*, char const*, HalFile&)",
    "HalFile::fileSize64()",
    "HalFile::isOpen() const",
    "HalFile::seek64(unsigned long long)",
    "HalFile::read(void*, unsigned int)",
    "HalFile::close()",
    "logPrintf(char const*, char const*, char const*, ...)",
    "operator new(unsigned int)",
    "operator delete(void*, unsigned int)",
]
if a.metadata:
    roots += ["xp_native_plugin_metadata"] + [
        n
        for n in f
        if n.startswith("PluginJson::api()::{lambda") and any(x in n for x in ("#1}", "#2}", "#5}"))
    ]
else:
    roots.extend(
        n
        for n in f
        if n
        in {
            "snprintf",
            "std::_Hash_bytes(void const*, unsigned int, unsigned int)",
            "HalFile::modificationTime()",
            "operator new[](unsigned int, std::nothrow_t const&)",
            "HalFile::size()",
            "HalStorage::openFileForWrite(char const*, char const*, HalFile&)",
            "HalFile::write(unsigned char)",
            "HalFile::write(unsigned char const*, unsigned int)",
            "HalStorage::rename(char const*, char const*)",
            "HalStorage::remove(char const*)",
            "HalStorage::exists(char const*)",
            "HalStorage::mkdir(char const*, bool)",
            "HalStorage::open(char const*, int)",
            "HalFile::openNextFile()",
            "HalFile::seek(unsigned int)",
            "HalFile::seekCur(long long)",
            "HalFile::getName(char*, unsigned int)",
            "HalFile::read()",
            "HalStorage::writeFile(char const*, String const&)",
            "HalStorage::readFile(char const*)",
            "HalFile::position() const",
        }
    )
alloc = [n for n in g if "DefaultAllocator::" in n]
for n in g:
    if "ArduinoJson::" in n and (
        u.get(n) or "StringNode::destroy(" in n or "ResourceManager::~ResourceManager" in n
    ):
        g[n].update(alloc)
    for part in ("::parseVariant<", "::VariantData::clear("):
        if part in n and n in g[n]:
            # Root parse at limit 10, terminal call when remaining reaches zero.
            g[n].remove(n)
            f[n]["frame"] *= 11
    if n == "HalFile::~HalFile()" and n in g[n]:
        g[n].remove(n)
        f[n]["frame"] *= 2
g["tlsf_walk_pool"].add("multi_heap_get_info_tlsf")
# Non-null walker argument supplied by multi_heap_get_info_impl.
g["tlsf_walk_pool"] -= {"default_walker"}
mask = None
if a.metadata:
    mask = next(n for n in f if n.startswith("xp_ffi::plugin::metadata_mask"))
    g[mask].update(n for n in roots if n.startswith("PluginJson"))
    roots += (
        [mask]
        + [n for n in f if "::parseVariant<" in n and n in g["xp_native_plugin_metadata"]]
        + [n for n in f if "VariantData::clear(" in n]
    )
for n in set(caps) | throw:
    g[n] = set()
for n in ["StreamFile<FsBaseFile, unsigned long long>::~StreamFile()", "FsFile::~FsFile()"]:
    assert n in g[n]
    g[n].remove(n)
    f[n]["frame"] *= 2
    notes.append(
        dict(
            function=n,
            reason="Merged deleting/nondeleting destructor names, two distinct nonrecursive bodies; charge both frames.",
        )
    )
# The real SD chip-select is an initialized physical GPIO, not Arduino's virtual RGB pin.
g["perimanGetPinBus"].discard("log_printf")
notes.append(
    dict(
        function="perimanGetPinBus",
        reason="Selected pin is a valid physical SD CS GPIO and bus type is constant ESP32_BUS_TYPE_GPIO; logging is only for invalid pin/type. This applies the same physical-GPIO predicate at the callee, not a logging failure exclusion.",
    )
)
g["__digitalWrite"] = {"perimanGetPinBus", "gpio_set_level"}
g["gpio_set_level"] = set()
notes.append(
    dict(
        function="__digitalWrite",
        reason="SDCardManager::begin uses ACTIVE.sd.cs as a physical GPIO and SdFat configures OUTPUT before cardReady. Physical GPIO is below SOC_GPIO_PIN_COUNT; RGB_BUILTIN is SOC_GPIO_PIN_COUNT+8. Valid GPIO bus/level excludes RMT and invalid-pin logging. Full32B digitalWrite and gpio_set_level frames charged.",
    )
)
g["rtc_clk_cpu_freq_get_config"] -= {"esp_log_timestamp", "abort"}
notes.append(
    dict(
        function="rtc_clk_cpu_freq_get_config",
        reason="Ordinary initialized CPU clock configuration: SDK XTAL/RC_FAST/PLL80/PLL160 sources, unchanged valid HAL power transitions. The error/ROM printf arm requires an unsupported hardware clock register state, which XTC input cannot install. Fixed frame and normal XTAL helper remain charged.",
    )
)
# Every printf reachable here is XTC diagnostics or the fixed SD-open failure format.
for n in ["_svfprintf_r", "_vfprintf_r"]:
    if n in g:
        removed = g[n] & {"_dtoa_r", "_ldtoa_r"}
        g[n] -= removed
        notes.append(
            dict(
                function=n,
                removed=sorted(removed),
                reason="Selected XTC diagnostic/SD-open formats use strings/integers only; generated thumbnail float logs are outside these HAL/resource roots.",
            )
        )
g["esp_timer_get_time"].add("systimer_ticks_to_us")
typed.append(
    dict(
        caller="esp_timer_get_time",
        callee="systimer_ticks_to_us",
        reason="Register tail loads systimer_hal.ticks_to_us. SDK early init installs exactly systimer_ticks_to_us with systimer_hal_set_tick_rate_ops; target frame0.",
    )
)
serial = "HWCDC::write(unsigned char const*, unsigned int)"
g["Print::vprintf(char const*, void*)"].add(serial)
typed.append(
    dict(
        caller="Print::vprintf(char const*, void*)",
        callee=serial,
        reason="SDCardManager Serial is the C3 USB Hardware CDC singleton; Print::printf virtual write selects this buffer overload.",
    )
)
g["Print::write(char const*)"].add(serial)
typed.append(
    dict(
        caller="Print::write(char const*)",
        callee=serial,
        reason="logSerial is the same Hardware CDC singleton. Register-tail virtual buffer-write slot+12 selects HWCDC::write; full32B Print frame remains charged.",
    )
)
ring = {
    "prvReceiveGeneric": ["prvGetItemByteBuf"],
    "prvSendAcquireGeneric": ["prvCheckItemFitsByteBuffer", "prvCopyItemByteBuf", "prvAcquireItemNoSplit"],
    "vRingbufferReturnItem": ["prvReturnItemByteBuf"],
    "xRingbufferGetCurFreeSize": ["prvGetCurMaxSizeByteBuf"],
}
for n, children in ring.items():
    g[n].update(children)
    typed.append(
        dict(
            caller=n,
            callees=children,
            reason="HWCDC begin creates tx_ring_buf as RINGBUF_TYPE_BYTEBUF; immutable ringbuffer dispatch slots. NoSplit acquire helper additionally charged conservatively.",
        )
    )
# Source initializes FsDateTime::callback to null; no linked setter/production caller.
null_callbacks = [
    "ExFatFile::openPrivate(ExFatFile*, ExName_t*, int)",
    "ExFatFile::syncDir()",
    "FatFile::open(FatFile*, FatLfn_t*, int)",
    "FatFile::sync()",
]
for n in null_callbacks:
    notes.append(
        dict(
            function=n,
            reason="Only indirect call is FsDateTime::callback. BSS initialized null, no production setCallback caller; qualified ELF has callback BSS but no setter/callback2. Runtime null witness required in packet.",
            sites=u[n],
        )
    )
# readStop sets IDLE before cardCommand(CMD12), so recursive syncDevice immediately returns.
cycle = [
    "SdSpiCard::cardCommand(unsigned char, unsigned long)",
    "SdSpiCard::readStop()",
    "SdSpiCard::syncDevice()",
]
capsule = "SD read-stop state capsule"
members = set(cycle)
outside = set().union(*(g[n] - members for n in cycle))
f[capsule] = dict(frame=sum(f[n]["frame"] for n in cycle) + f[cycle[0]]["frame"])
g[capsule] = outside
for n in cycle:
    g[n] = {capsule}
    f[n]["frame"] = 0
notes.append(
    dict(
        function=capsule,
        members=cycle,
        reason="readStop first publishes IDLE_STATE; nested cardCommand->syncDevice returns immediately. Charge cardCommand twice and readStop/syncDevice once plus maximum full outside tail; WRITE_STATE writeStop remains an outside branch.",
    )
)
# ESP32-C3 ROM is immutable. Read the qualified maximum across the same silicon revisions.
rf = json.loads((Path(__file__).parent / "data/rom.json").read_text())


def rombound(n):
    return rf[n]


if not a.metadata:
    n = "Print::print(String const&)"
    target = "StreamFile<FsBaseFile, unsigned long long>::write(unsigned char const*, unsigned int)"
    g[n].add(target)
    typed.append(
        dict(
            caller=n,
            callee=target,
            reason="SDCardManager writes state through f.print(content), f is the local FsFile.",
        )
    )
reached = reachable(g, roots)
external = []
for n in list(reached):
    if n in caps or n in throw or n == capsule:
        continue
    for c in f[n].get("calls", []):
        if not c.get("target") or c.get("name") in f:
            continue
        z = c["name"]
        if z == "esp_rom_printf" and n == "rtc_clk_cpu_freq_get_config":
            continue
        if z == "esp_rom_printf" and n in ["assert_valid_block", "verify_allocated_region"]:
            notes.append(
                dict(
                    function=n,
                    excluded=z,
                    reason="Requires corrupted allocator block; valid-allocation predicate inherited from complete fatal method.",
                )
            )
            continue
        if n in ["_svfprintf_r", "_vfprintf_r"] and re.fullmatch(r"__(?:lt|le|gt|ge|eq|ne|unord)df2", z):
            continue
        assert z in rf, (n, z)
        key = "ROM::" + z
        f[key] = dict(frame=0)
        g[key] = set()
        caps[key] = rombound(z)
        g[n].add(key)
        external.append(dict(caller=n, callee=z, bound=caps[key]))
reached = reachable(g, roots)
cycles = [v for v in scc(g, reached) if len(v) > 1 or v[0] in g[v[0]]]
assert not cycles, cycles
unknown = {n: u[n] for n in sorted(reached) if u.get(n) and n not in caps and n not in throw}
allowed = (
    set(null_callbacks)
    | set(ring)
    | {"Print::vprintf(char const*, void*)", "tlsf_realloc", "operator new(unsigned int)"}
)
allowed.update(n for n in unknown if "ArduinoJson::" in n)
allowed.update(["tlsf_walk_pool", mask])
assert set(unknown) <= allowed, set(unknown) - allowed
notes.append(
    dict(
        function="operator new(unsigned int)",
        reason="Installed new_handler is null in the qualified runtime witness; its indirect callback cannot execute. Ordinary malloc return and actual bad_alloc throw edges are classified separately, never replaced by a mixed shared capsule.",
    )
)
notes.append(
    dict(
        function="tlsf_realloc",
        sites=unknown.get("tlsf_realloc", []),
        reason="Unresolved weak poison hooks are linked literal zero and each branch skips its jalr; per-site disassembly retained. No callable target.",
    )
)
register_tails = []
for n in sorted(reached):
    if n not in b or n in caps or n in throw:
        continue
    for line in b[n].splitlines():
        if re.search(r"\bjr\s+(?!ra\b)", line) and not re.search(r"\b[0-9a-f]{8}\s+<", line):
            assert (
                "StringNode::destroy(" in n
                or "ResourceManager::~ResourceManager" in n
                or n
                in {
                    "Print::write(char const*)",
                    "Print::print(String const&)",
                    "esp_timer_get_time",
                    "_svfprintf_r",
                    "get_arg.isra.0",
                }
            ), (n, line)
            why = (
                "Default JsonDocument allocator dispatch, charge all three slot continuations."
                if "ArduinoJson::" in n
                else "Exact typed callback above"
                if n in {"Print::write(char const*)", "esp_timer_get_time"}
                else "Immutable local switch jump table; all case bodies are in this function and its complete direct-call inventory, not a function-pointer callback."
            )
            register_tails.append(dict(function=n, instruction=line.strip(), disposition=why))
memo = {}


def bound(n, kind):
    key = n, kind
    if key in memo:
        return memo[key]
    if n in throw:
        result = (fatal["C_fatal"], [n]) if kind == "fatal" else (-(10**9), [])
    elif n in caps:
        result = (caps[n], [n]) if kind == "returning" else (-(10**9), [])
    elif not g[n]:
        result = (f[n]["frame"], [n]) if kind == "returning" else (-(10**9), [])
    else:
        cost, path = max((bound(z, kind) for z in g[n]))
        result = (f[n]["frame"] + cost, [n] + path)
    memo[key] = result
    return result


result = dict(
    elf_sha256=hashlib.sha256((a.image / "firmware.elf").read_bytes()).hexdigest(),
    C_fatal=fatal["C_fatal"],
    roots={
        n: {k: dict(bytes=bound(n, k)[0], chain=bound(n, k)[1]) for k in ["returning", "fatal"]}
        for n in roots
    },
    typed=typed,
    register_tails=register_tails,
    dispositions=notes,
    external=external,
    unknown=unknown,
    functions=[
        dict(name=n, frame=f[n]["frame"], callees=sorted(g[n]), shared_capsule=caps.get(n))
        for n in sorted(reached)
    ],
    scope="Physical tails only; selected XTC task prefixes and component frames must be added. Valid initialized X3/C3 SD/GPIO/allocator/runtime state, no allocation-failure exclusion. Date callback runtime witness and exception no-landing-pad proof remain separate.",
)
(a.out / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n")
(a.out / "disassembly.txt").write_text("\n".join(b[n] for n in sorted(reached) if n in b))
print({n: {k: v["bytes"] for k, v in row.items()} for n, row in result["roots"].items()})
