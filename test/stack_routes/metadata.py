"""Validate the linked sleep-entry path to the plugin sidecar and document callbacks."""

import hashlib
import json
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

root = Path(sys.argv[1]) / "stack"
result = {}
for side in ("candidate",):
    build = root / side / "default"
    disassembly = (build / "all-disassembly.txt").read_text()
    calls = defaultdict(list)
    frames = {}
    addresses = {}
    elf = (build / "firmware.elf").read_bytes()
    phoff = struct.unpack_from("<I", elf, 28)[0]
    phsize, phnum = struct.unpack_from("<HH", elf, 42)
    segments = [struct.unpack_from("<8I", elf, phoff + i * phsize) for i in range(phnum)]
    symbols = (build / "symbols.txt").read_text().splitlines()

    def slot_address(symbol, offset):
        candidates = [int(line.split()[0], 16) for line in symbols if line.endswith(" " + symbol)]
        assert len(candidates) == 1, (symbol, candidates)
        address = candidates[0] + offset
        for kind, pos, virtual, _, size, *_ in segments:
            if kind == 1 and virtual <= address and address + 4 <= virtual + size:
                return struct.unpack_from("<I", elf, pos + address - virtual)[0]
        raise AssertionError((symbol, offset))

    for line in (build / "resolved-calls.jsonl").open():
        row = json.loads(line)
        name = row["name"]
        frames[name] = max(row["frame"], frames.get(name, 0))
        addresses[int(row["address"], 16)] = name
        calls[name].extend(row["calls"])

    def body(name):
        return disassembly.split(f"<{name}>:\n", 1)[1].split("\n\n", 1)[0].splitlines()

    def witness(parent, target):
        lines = [
            line
            for line in body(parent)
            if re.search(r"\b(?:j|jr|jal|jalr)\s+(?:.*?#\s*)?[a-f0-9]+ <" + re.escape(target) + r">$", line)
        ]
        assert lines, (parent, target)
        return {
            "caller": parent,
            "frame": frames[parent],
            "target": target,
            "target_frame": frames[target],
            "instructions": lines,
        }

    def register_call(parent, predicate):
        lines = body(parent)
        return [
            lines[max(0, i - 12) : i + 1]
            for i, line in enumerate(lines)
            if (match := re.search(r"\b(?:jalr|jr)\s+(?!ra$)([a-z][a-z0-9]*)$", line))
            and predicate(match[1], lines[max(0, i - 18) : i])
        ]

    def loaded_from(reg, lines, offset, base=r"\w+"):
        writes = [
            line
            for line in lines
            if re.search(r"\b(?:lw|mv|li|add|addi|lui|auipc|and|or|xor)\s+" + reg + ",", line)
        ]
        return bool(
            writes and re.search(r"\blw\s+" + reg + "," + str(offset) + r"\(" + base + r"\)", writes[-1])
        )

    def find(pattern):
        matches = [name for name in frames if re.fullmatch(pattern, name)]
        assert len(matches) == 1, (pattern, matches)
        return matches[0]

    def route(*names):
        return {
            "frames": [{"name": n, "bytes": frames[n]} for n in names],
            "sum": sum(frames[n] for n in names),
        }

    slot = find(
        r"\(anonymous namespace\)::appRuntime\(\)::\{lambda\(unsigned char\)#6\}::_FUN\(unsigned char\)"
    )
    sleep = find(r"enterDeepSleep\(bool\)")
    emit = find(r"pluginevents::emit\(pluginevents::Event, pluginevents::Var const\*, unsigned int\)")
    # offsetof(XpAppRuntime, sleep_enter) is 0x64 in the X3 compiler.
    runtime_address = slot_address("(anonymous namespace)::appRuntime()::runtime", 0x64)
    assert addresses[runtime_address] == slot
    # Dynamic Runtime.sleep_enter is locked to its verified native lambda, which tail-jumps to enterDeepSleep.
    start = disassembly.index(f"<{slot}>:")
    assert re.search(
        r"^.*\bj\s+[a-f0-9]+ <enterDeepSleep\(bool\)>$", disassembly[start:].split("\n\n", 1)[0], re.M
    )
    worker = [call["name"] for call in calls[sleep] if "deliverSleepPluginEvents" in call["name"]]
    assert len(worker) <= 1
    path = [slot, sleep, *worker, emit, "xp_plugin_emit"]

    def direct(parent, child):
        witnesses = [c for c in calls[parent] if c["name"] == child]
        assert witnesses, (parent, child)
        return witnesses

    for a, b in zip(path[1:], path[2:]):
        direct(a, b)
    task = "loopTask(void*)"
    app_wrapper = "loop()"
    manager = "ActivityManager::loop()"
    app = "xp_app_loop"
    direct(task, app_wrapper)
    for parent, target in [(app_wrapper, manager), (manager, app)]:
        addr = disassembly.index(f"<{parent}>:")
        excerpt = disassembly[addr:].split("\n\n", 1)[0]
        assert re.search(
            r"\b(?:j|jr|jal|jalr)\s+(?:.*?#\s*)?[a-f0-9]+ <" + re.escape(target) + r">", excerpt
        ), (parent, target, excerpt)
    assert any(c["name"] == "indirect" for c in calls[app])
    # Runtime.sleep_enter is the pre-existing linked table slot. The exact
    # source binding is src/main.cpp's .sleep_enter lambda.
    path = [task, app_wrapper, manager, app] + path
    branches = {"emitter": route(*path)}
    edges = [witness(a, b) for a, b in zip(path, path[1:]) if a != app]
    runtime_instructions = register_call(app, lambda reg, lines: loaded_from(reg, lines, 100))
    assert runtime_instructions, app
    edges.append(
        {
            "caller": app,
            "frame": frames[app],
            "target": slot,
            "target_frame": frames[slot],
            "address": hex(runtime_address),
            "table": "appRuntime()::runtime + 0x64",
            "instructions": runtime_instructions,
        }
    )
    if side == "candidate":
        mask = find(r"xp_ffi::plugin::metadata_mask(?:\.part\.\d+)?")
        direct("xp_plugin_emit", mask)
        reader = "xp_native_plugin_metadata"
        direct(mask, reader)
        edges += [witness("xp_plugin_emit", mask), witness(mask, reader)]
        for leaf in calls[reader]:
            if leaf["name"] == "indirect" or leaf["name"] not in frames:
                continue
            label = f"native: {leaf['name']}"
            branches[label] = route(*path, mask, reader, leaf["name"])
        branches["metadata document"] = route(*path, mask, reader)
        guard = "xp_sensitive_path"
        direct(mask, guard)
        edges.append(witness(mask, guard))
        branches["metadata path guard"] = route(*path, mask, guard)
        hal_open = "HalStorage::openFileForRead(char const*, char const*, HalFile&)"
        sdk_open = "SDCardManager::openFileForRead(char const*, char const*, FsFile&)"
        direct(hal_open, sdk_open)
        edges += [witness(reader, hal_open), witness(hal_open, sdk_open)]
        branches["SDK open adapter"] = route(*path, mask, reader, hal_open, sdk_open)
        assert sum(call["name"] == "indirect" for call in calls[mask]) >= 3
        for label, offset in [("text", 4), ("integer", 8), ("pair", 16)]:
            addr = slot_address("PluginJson::api()::table", offset)
            target = addresses[addr]
            assert target.startswith("PluginJson::api()::{lambda"), target
            branches[f"JSON adapter {label}"] = route(*path, mask, target)
            instructions = register_call(mask, lambda reg, lines: loaded_from(reg, lines, offset, "s5"))
            assert instructions, (mask, label)
            edges.append(
                {
                    "caller": mask,
                    "frame": frames[mask],
                    "target": target,
                    "target_frame": frames[target],
                    "address": hex(addr),
                    "table": f"PluginJson::api()::table + {offset}",
                    "instructions": instructions,
                }
            )

    result[side] = {"elf_sha256": hashlib.sha256(elf).hexdigest(), "branches": branches, "edges": edges}
    print(side, {k: v.get("sum", v) if isinstance(v, dict) else v for k, v in branches.items()})
(root.parent / "SLEEP-STACK.json").write_text(json.dumps(result, indent=2) + "\n")
