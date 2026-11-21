import hashlib
import json
import re
import struct
import subprocess as sp
from pathlib import Path


class ConfigError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ConfigError(message)


def digest(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def load_json(path):
    return json.loads(path.read_text())


def save_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n")


def _tool(prefix, name, *args):
    return sp.check_output([prefix + name, *map(str, args)], text=True, errors="replace")


def _prefix(cfg, env):
    arch = cfg.raw("linked.env_arch")[env]
    return cfg.raw(f"toolchain.{arch}_prefix")


def _arch(cfg, env):
    return cfg.raw("linked.env_arch")[env]


def _patterns(value):
    return [name for name in value]


def _resolve_frame(name, frames):
    """Exact name, else one unique suffix, else one unique substring."""
    if name in frames:
        return name
    endings = [candidate for candidate in frames if candidate.endswith(name)]
    if len(endings) == 1:
        return endings[0]
    found = [candidate for candidate in frames if name in candidate]
    if len(found) == 1:
        return found[0]
    raise ConfigError(f"route step {name!r} does not resolve uniquely: {found[:4]}")


def _elf_words(data, address, count):
    phoff = struct.unpack_from("<I", data, 28)[0]
    phsize, phnum = struct.unpack_from("<HH", data, 42)
    for index in range(phnum):
        segment = struct.unpack_from("<IIIIIIII", data, phoff + index * phsize)
        if segment[0] == 1 and segment[2] <= address and address + 4 * count <= segment[2] + segment[4]:
            return list(struct.unpack_from("<" + str(count) + "I", data, segment[1] + address - segment[2]))
    return None


def inspect(cfg, env, directory):
    """Sections, symbols, leaf table, DWARF layouts and fixed frames for one image."""
    prefix = _prefix(cfg, env)
    arch = _arch(cfg, env)
    elf = directory / "firmware.elf"
    require(elf.is_file(), f"{elf}: missing")
    section_text = _tool(prefix, "size", "-A", elf)
    (directory / "sections.txt").write_text(section_text)
    symbols = _tool(prefix, "nm", "-S", "-C", elf)
    (directory / "symbols.txt").write_text(symbols)
    retired = cfg.raw("linked.retired_exports")
    hits = [name for name in retired if re.search(r"\b" + re.escape(name) + r"\b", symbols)]
    require(not hits, f"{env}: retired exports still present: {hits[:5]}")
    for forbidden in cfg.raw("linked.forbidden_symbol_patterns"):
        require(forbidden not in symbols, f"{env}: unexpected probe symbol {forbidden}")
    save_json(
        directory / "retired-symbol-absence.json",
        dict(retired_exports=retired, hits=hits, elf_sha256=digest(elf)),
    )
    probe = [
        line
        for line in symbols.splitlines()
        if any(
            token in line
            for token in cfg.raw(
                "linked.probe_tokens", ["rustEpubResources", "xp_app_render", "xp_ffi::app::APP"]
            )
        )
    ]
    (directory / "probe-symbols.txt").write_text("\n".join(probe) + "\n")
    sections = {}
    for line in section_text.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0].startswith("."):
            sections[parts[0]] = dict(size=int(parts[1]), address=int(parts[2]))
    table = None
    fields = cfg.raw("linked.fields")
    for line in probe:
        if line.endswith("::leaves"):
            address, size, kind, _ = line.split(None, 3)
            address, size = int(address, 16), int(size, 16)
            containers = [
                name
                for name, section in sections.items()
                if "dummy" not in name
                and section["address"] <= address
                and address + size <= section["address"] + section["size"]
            ]
            table = dict(address=hex(address), size=size, nm_type=kind, sections=containers)
    if table:
        require(table["size"] == 4 * fields, f"{env}: leaf table size {table['size']} != {4 * fields}")
        require(".flash.rodata" in table["sections"], f"{env}: leaf table not in rodata")
        leaves = _elf_words(elf.read_bytes(), int(table["address"], 16), fields)
        require(
            leaves and len(leaves) == fields and 0 not in leaves, f"{env}: incomplete production leaf table"
        )
        table["leaf_addresses"] = [hex(value) for value in leaves]
    save_json(
        directory / "table-placement.json",
        dict(
            table=table,
            data_bss=sum(sections.get(name, {}).get("size", 0) for name in (".dram0.data", ".dram0.bss")),
            noinit=sections.get(".noinit", {}).get("size", 0),
            table_required=True,
        ),
    )
    frames = _frames(cfg, prefix, elf, arch, directory)
    save_json(
        directory / "inspection.json",
        dict(
            arch=arch,
            elf_sha256=digest(elf),
            frames=frames,
            note="Fixed prologue observations only; indirect call-chain totals and actual unused "
            "stack require separate attribution.",
        ),
    )
    return sections, table, frames


def _frames(cfg, prefix, elf, arch, directory):
    # Every function is kept: a leaf or a route step may be any linked function.
    proc = sp.Popen([prefix + "objdump", "-d", "-C", str(elf)], stdout=sp.PIPE, text=True)
    header = re.compile(r"^([0-9a-f]+) <(.+)>:$")
    address = name = None
    lines, frames, selected, raw = [], [], [], []
    while True:
        line = proc.stdout.readline()
        if not line:
            break
        match = header.match(line.rstrip())
        if match:
            if name is not None:
                selected.append(_frame_row(name, address, lines, frames))
                raw.extend(lines)
            address, name, lines, frames = match.group(1), match.group(2), [line], []
            continue
        if name is None:
            continue
        lines.append(line)
        found = re.search(r"\b(?:addi?|c\.addi16sp)\s+sp,\s*sp,\s*-(0x[0-9a-f]+|[0-9]+)", line)
        if found:
            frames.append(int(found.group(1), 0))
        found = re.search(r"\bentry\s+a1,\s*(0x[0-9a-f]+|[0-9]+)", line)
        if found:
            frames.append(int(found.group(1), 0))
    if name is not None:
        selected.append(_frame_row(name, address, lines, frames))
        raw.extend(lines)
    require(proc.wait() == 0, f"{elf}: objdump failed")
    (directory / "all-disassembly.txt").write_text("".join(raw))
    cfi = _tool(prefix, "readelf", "--debug-dump=frames-interp", elf)
    (directory / "frames-interp.txt").write_text(cfi)
    by_address = {int(row["address"], 16): row for row in selected}
    current = None
    for line in cfi.splitlines():
        match = re.search(r"FDE .*pc=([0-9a-f]+)\.\.([0-9a-f]+)", line)
        if match:
            current = by_address.get(int(match.group(1), 16))
            continue
        if current is not None:
            match = re.match(r"[0-9a-f]+\s+sp\+([0-9]+)\s", line)
            if match:
                current["dwarf_cfa_max"] = max(current.get("dwarf_cfa_max", 0), int(match.group(1)))
    for row in selected:
        row["fixed_frame"] = (
            row["entry_frame"] if arch == "s3" else row.get("dwarf_cfa_max", row["max_fixed_adjustment"])
        )
        row["method"] = (
            "Xtensa first-instruction entry"
            if arch == "s3"
            else "DWARF CFA maximum"
            if "dwarf_cfa_max" in row
            else "stack adjustment; verify multi-adjustment frames"
        )
    return selected


def _frame_row(name, address, lines, frames):
    entry = re.search(r"\bentry\s+a1,\s*(0x[0-9a-f]+|[0-9]+)", lines[1] if len(lines) > 1 else "")
    return dict(
        name=name,
        address=address,
        stack_adjustments=frames[:],
        max_fixed_adjustment=max(frames, default=0),
        entry_frame=int(entry.group(1), 0) if entry else 0,
    )


def _physical_table_leaf(cfg, directory, spec, field, address, frames, leaf_source):
    """Resolve an explicitly typed data pointer without treating it as a frame."""
    typename, symbol = spec["type"], spec["symbol"]
    require(
        re.search(
            r"\bconst\s+" + re.escape(typename) + r"\s*\*\s*" + re.escape(field) + r"\s*;", leaf_source
        ),
        f"{field}: physical-table pointer declaration differs",
    )
    header = cfg.build_path / spec["header"]
    raw = header.read_bytes()
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", raw.decode(), flags=re.S)
    match = re.search(
        r"typedef\s+struct\s+"
        + re.escape(typename)
        + r"\s*\{([^{}]*)\}\s*"
        + re.escape(typename)
        + r"\s*;",
        text,
        re.S,
    )
    require(match, f"{header}: missing named physical table {typename}")
    declarations = [item.strip() for item in match[1].split(";") if item.strip()]
    members = [re.search(r"\(\s*\*\s*(\w+)\s*\)", item) for item in declarations]
    require(members and all(members), f"{typename}: physical table contains a non-callable member")
    names = [item[1] for item in members]
    require(len(names) == len(set(names)), f"{typename}: duplicate member")
    size = 4 * len(names)
    symbols = [line.split(None, 3) for line in (directory / "symbols.txt").read_text().splitlines()]
    found = [row for row in symbols if len(row) == 4 and row[3] == symbol]
    require(
        len(found) == 1
        and int(found[0][0], 16) == int(address, 16)
        and int(found[0][1], 16) == size
        and found[0][2] in ("r", "R"),
        f"{field}: physical-table address/size/read-only symbol differs",
    )
    sections = [line.split() for line in (directory / "sections.txt").read_text().splitlines()]
    require(
        any(
            len(row) >= 3
            and row[0] == ".flash.rodata"
            and int(row[2]) <= int(address, 16)
            and int(address, 16) + size <= int(row[2]) + int(row[1])
            for row in sections
        ),
        f"{field}: physical table is not in flash rodata",
    )
    words = _elf_words((directory / "firmware.elf").read_bytes(), int(address, 16), len(names))
    require(words and 0 not in words, f"{field}: incomplete physical table")
    records = []
    for index, (name, pointer) in enumerate(zip(names, words)):
        frame = frames.get(pointer)
        require(frame, f"{field}.{name}: pointer {hex(pointer)} is not a linked function")
        records.append(
            dict(
                index=index,
                field=name,
                address=hex(pointer),
                function=frame["name"],
                fixed_frame=frame["fixed_frame"],
                frame_method=frame["method"],
            )
        )
    return dict(
        kind="physical_table",
        type=typename,
        symbol=symbol,
        table_bytes=size,
        header_sha256=hashlib.sha256(raw).hexdigest(),
        members=records,
    )


def map_leaves(cfg, directory, fields):
    """One record per production leaf field: the linked function it calls."""
    header = cfg.build_path / cfg.raw("linked.leaf_header")
    macro = cfg.raw("linked.leaf_macro")
    source = header.read_text()
    names = [
        (int(index), name) for name, index in re.findall(r"^" + macro + r"\((\w+), (\d+)\);", source, re.M)
    ]
    require(
        [index for index, _ in names] == list(range(fields)),
        f"{header}: leaf offsets are not 0..{fields - 1}",
    )
    placement = load_json(directory / "table-placement.json")["table"]
    inspection = load_json(directory / "inspection.json")
    frames = {int(row["address"], 16): row for row in inspection["frames"]}
    if "leaf_addresses" not in placement:
        raise ConfigError(f"{directory}: no leaf addresses")
    require(len(placement["leaf_addresses"]) == len(names), "leaf table/field count differ")
    physical_tables = cfg.raw("linked.physical_tables", {})
    require(
        set(physical_tables) <= {name for _, name in names}, "configured physical table is not an EPUB leaf"
    )
    records = []
    for (index, name), address in zip(names, placement["leaf_addresses"]):
        if name in physical_tables:
            record = _physical_table_leaf(
                cfg, directory, physical_tables[name], name, address, frames, source
            )
            records.append(dict(index=index, field=name, address=address, **record))
            continue
        frame = frames.get(int(address, 16))
        require(frame, f"{directory}: leaf {name} at {address} is not a selected frame")
        records.append(
            dict(
                index=index,
                field=name,
                address=address,
                function=frame["name"],
                fixed_frame=frame["fixed_frame"],
                frame_method=frame["method"],
            )
        )
    save_json(
        directory / "linked-epub-leaves.json",
        dict(
            elf_sha256=inspection["elf_sha256"],
            header_sha256=hashlib.sha256(source.encode()).hexdigest(),
            table_address=placement["address"],
            table_bytes=placement["size"],
            leaves=records,
        ),
    )
    return records


def resolved_calls(directory):
    """Direct calls and S3 literal-pool call targets per selected frame."""
    inspection = load_json(directory / "inspection.json")
    frames = {int(row["address"], 16): row for row in inspection["frames"]}
    data = (directory / "firmware.elf").read_bytes()
    phoff = struct.unpack_from("<I", data, 28)[0]
    phsize, phnum = struct.unpack_from("<HH", data, 42)
    segments = []
    for index in range(phnum):
        values = struct.unpack_from("<IIIIIIII", data, phoff + index * phsize)
        if values[0] == 1:
            segments.append((values[2], values[1], values[4]))

    def word(address):
        for start, offset, size in segments:
            if start <= address and address + 4 <= start + size:
                return struct.unpack_from("<I", data, offset + address - start)[0]
        return None

    rows = []
    for block in re.split(r"\n(?=[0-9a-f]+ <)", (directory / "all-disassembly.txt").read_text()):
        lines = block.splitlines()
        if not lines:
            continue
        head = re.match(r"([0-9a-f]+) <(.+)>:", lines[0])
        if not head or int(head[1], 16) not in frames:
            continue
        address = int(head[1], 16)
        frame = frames[address]
        calls, registers = [], {}
        for line in lines[1:]:
            instruction = re.match(r"\s*([0-9a-f]+):\s+[0-9a-f]+\s+([a-z0-9.]+)\s*(.*)", line)
            if not instruction:
                continue
            pc, op, arguments = instruction.groups()
            if op == "l32r":
                match = re.match(r"(a\d+),\s*([0-9a-f]+)", arguments)
                if match:
                    registers[match[1]] = word(int(match[2], 16))
            if op.startswith("call") or op in ("jal", "jalr"):
                direct = re.search(r"(?:#\s*)?([0-9a-f]{8}) <([^>]+)>", arguments)
                target = (
                    int(direct[1], 16)
                    if direct
                    else registers.get(arguments.strip())
                    if op.startswith("callx")
                    else None
                )
                callee = frames.get(target, {})
                calls.append(
                    dict(
                        pc=pc,
                        op=op,
                        target=hex(target) if target is not None else None,
                        name=callee.get("name", direct[2] if direct else "indirect"),
                        frame=callee.get("fixed_frame"),
                    )
                )
                if op.startswith("call"):
                    for register in list(registers):
                        if int(register[1:]) >= 8:
                            registers.pop(register, None)
            elif op not in ("l32r", "s32i", "s32i.n", "s16i", "s8i", "j", "jx") and not op.startswith(
                ("b", "ret")
            ):
                dest = re.match(r"(a\d+),", arguments)
                if dest:
                    registers.pop(dest[1], None)
        rows.append(
            dict(
                name=frame["name"],
                address=frame["address"],
                frame=frame["fixed_frame"],
                method=frame["method"],
                adjustments=frame["stack_adjustments"],
                calls=calls,
            )
        )
    with (directory / "resolved-calls.jsonl").open("w") as out:
        for row in rows:
            out.write(json.dumps(row) + "\n")
    return rows


def _per_env(cfg, key, env):
    value = cfg.raw(key)
    target = value[env] if isinstance(value, dict) else value
    return Path(target) if isinstance(target, str) else target


def _route_table(cfg, env):
    source = _per_env(cfg, "linked.routes_source", env)
    document = load_json(source)
    require("routes" in document, f"{source}: no routes table")
    return source, document["routes"]


def routes(cfg, directory, inspection, leaves, calls):
    """Resolve the curated route table in this image and audit every edge."""
    source, table = _route_table(cfg, directory.name)
    frames = {}
    for row in inspection["frames"]:
        if row["name"] not in frames or row["fixed_frame"] > frames[row["name"]]["fixed_frame"]:
            frames[row["name"]] = row
    selected = {}
    for label, row in table.items():
        patterns = row["segments"] if "segments" in row else [frame["name"] for frame in row["frames"]]
        chain = []
        for pattern in patterns:
            name = _resolve_frame(pattern, frames)
            chain.append(
                dict(name=name, address=frames[name]["address"], frame=frames[name]["fixed_frame"])
            )
        selected[label] = dict(
            task=row.get("task", "render"),
            total=sum(item["frame"] for item in chain),
            frames=chain,
            notes=row.get("notes", ""),
            edge_review_pending=True,
        )
    audit_source = (
        _per_env(cfg, "linked.attestations_source", directory.name)
        if cfg.raw("linked.attestations_source", None)
        else None
    )
    attestations = {}
    if audit_source:
        document = load_json(audit_source)
        for edge in document.get("edges", []):
            if edge.get("kind") and edge.get("caller") and edge.get("callee"):
                # A curated attestation carries its source, or the prior
                # evidence record that qualified it. Either is accepted data.
                if edge.get("source") or edge.get("prior"):
                    attestations[(edge["caller"], edge["callee"])] = edge
    call_index = {row["name"]: row for row in calls}
    by_address = {int(row["address"], 16): row["name"] for row in inspection["frames"]}
    tails, tail_lines = {}, {}
    for block in re.split(r"\n(?=[0-9a-f]+ <)", (directory / "all-disassembly.txt").read_text()):
        lines = block.splitlines()
        head = re.match(r"([0-9a-f]+) <(.+)>:", lines[0]) if lines else None
        if not head:
            continue
        for line in lines[1:]:
            if not re.search(r"\b(j|jr|jx)\b", line):
                continue
            target = re.search(r"(?:#\s*)?([0-9a-f]{8}) <", line)
            if target and int(target[1], 16) in by_address:
                callee = by_address[int(target[1], 16)]
                tails.setdefault(head[2], set()).add(callee)
                tail_lines.setdefault((head[2], callee), []).append(line.strip())
    edges, unresolved = {}, []
    for label, route in selected.items():
        names = [item["name"] for item in route["frames"]]
        for caller, callee in zip(names, names[1:]):
            edges.setdefault((caller, callee), []).append(label)
    rows = []
    for (caller, callee), labels in edges.items():
        address = int(frames[callee]["address"], 16)
        found = [
            call
            for call in call_index.get(caller, {}).get("calls", [])
            if call.get("target") is not None
            and (int(call["target"], 16) == address or call["name"] == callee)
        ]
        row = dict(
            caller=caller,
            callee=callee,
            routes=labels,
            calls=found,
            tails=tail_lines.get((caller, callee), []),
        )
        attested = attestations.get((caller, callee))
        if found or callee in tails.get(caller, set()):
            row["kind"] = "current linked call/tail; full frames conservatively charged"
        elif attested:
            row["kind"] = attested["kind"]
            for key in ("source", "mechanism", "prior"):
                if attested.get(key):
                    row[key] = attested[key]
        else:
            row["kind"] = "unresolved"
            unresolved.append(dict(caller=caller, callee=callee, routes=labels))
        rows.append(row)
    save_json(
        directory / "selected-production-chains.json",
        dict(
            elf_sha256=inspection["elf_sha256"],
            routes=selected,
            source=str(source),
            scope="Selected production routes; each edge linked or attested in the config. "
            "Not an exhaustive call graph or a runtime watermark.",
        ),
    )
    save_json(
        directory / "selected-edge-audit.json",
        dict(
            elf_sha256=inspection["elf_sha256"],
            edges=rows,
            unresolved=unresolved,
            attested=sum(1 for row in rows if row.get("source") or row.get("prior")),
        ),
    )
    require(not unresolved, f"{directory}: unresolved route edges: {unresolved[:3]}")
    for row in selected.values():
        row["edge_review_pending"] = False
    return selected, rows
