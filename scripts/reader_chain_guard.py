#!/usr/bin/env python3
"""Audit linked default-X3 reader language crossings (standard library only)."""

import argparse
import bisect
import collections
import hashlib
import json
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = pathlib.Path(__file__).with_name("reader_chain_guard")
OWNER_TASKS = {
    "inherited": {"R1", "R1b", "R2", "R3a", "R3b", "R4"},
    "deferred": {"R5", "R6", "R7"},
}


def stable_name(name):
    """Rust's legacy symbol hash is a build detail, not a boundary identity."""
    name = re.sub(r"::h[0-9a-f]{16}(?=\b|$)", "", name)
    return re.sub(r"\.llvm\.[0-9]+", ".llvm", name)


class Elf:
    """Minimal ELF32 little-endian reader; reject other architectures explicitly."""

    def __init__(self, path):
        self.data = pathlib.Path(path).read_bytes()
        if self.data[:6] != b"\x7fELF\x01\x01":
            raise ValueError("expected an unstripped little-endian ELF32")
        if struct.unpack_from("<H", self.data, 18)[0] != 243:
            raise ValueError("expected the default-X3 RISC-V ELF")
        shoff = struct.unpack_from("<I", self.data, 32)[0]
        shsize, shnum = struct.unpack_from("<HH", self.data, 46)
        self.sections = [
            struct.unpack_from("<10I", self.data, shoff + i * shsize)
            for i in range(shnum)
        ]
        self.symbols = []
        for section in self.sections:
            if section[1] != 2:  # SHT_SYMTAB
                continue
            strings = self.section_data(self.sections[section[6]])
            for offset in range(section[4], section[4] + section[5], section[9]):
                name, address, size, info, _, index = struct.unpack_from(
                    "<IIIBBH", self.data, offset
                )
                if not index or not name:
                    continue
                end = strings.index(0, name)
                self.symbols.append(
                    (strings[name:end].decode(), address, size, info & 15)
                )
        if not self.symbols:
            raise ValueError("ELF has no symbol table")

    def section_data(self, section):
        return self.data[section[4] : section[4] + section[5]]

    def read(self, address, size):
        for section in self.sections:
            if section[1] == 8:  # SHT_NOBITS
                continue
            if section[3] <= address and address + size <= section[3] + section[5]:
                offset = section[4] + address - section[3]
                return self.data[offset : offset + size]
        raise ValueError(f"unmapped ELF address {address:#x}, size {size}")


def map_inventory(text):
    """Input-section ranges, including archive member provenance, from GNU ld."""
    ranges = []
    objects = set()
    for line in text.splitlines():
        if line.startswith("LOAD "):
            objects.add(line[5:].strip())
        match = re.match(
            r"\s+(?:\.\S+\s+)?(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.+?)\s*$", line
        )
        if match:
            start, size = int(match[1], 16), int(match[2], 16)
            obj = match[3]
            if start and size and re.search(r"\.(?:o|a)(?:\(.*\))?$", obj):
                ranges.append((start, start + size, obj))
    return sorted(ranges), sorted(objects)


def object_language(obj):
    # rustc archive members, including compiler_builtins, carry .rcgu.o names.
    if re.search(r"(?:^|[./])[^/]*\.ltrans(?:\d+)?(?:\.|$)", obj):
        return "unknown"
    if ".rcgu.o" in obj:
        return "rust"
    if obj:
        return "native"
    return "unknown"


class ReferenceSymbols:
    """Resolve only addresses owned by ELF symbols, never objdump's nearest label."""

    def __init__(self, symbols, names, sections=()):
        self.symbols = sorted(
            (address, size, stable_name(names[raw]))
            for raw, address, size, _ in symbols
        )
        self.starts = [s[0] for s in self.symbols]
        self.data_ranges = [
            (s[3], s[3] + s[5]) for s in sections if s[2] & 2 and not s[2] & 4
        ]
        self.max_ends = []
        end = 0
        for start, size, _ in self.symbols:
            end = max(end, start + max(size, 1))
            self.max_ends.append(end)

    def resolve(self, address):
        pos = bisect.bisect_right(self.starts, address) - 1
        while pos >= 0 and address < self.max_ends[pos]:
            start, size, name = self.symbols[pos]
            if address == start or address < start + size:
                return name + (f"+{address - start:#x}" if address != start else "")
            pos -= 1
        return None

    def is_data(self, address):
        return any(start <= address < end for start, end in self.data_ranges)


def parse_disassembly(text, provenance, symbols=None):
    """Retain calls, cross-function branches/tails, and address-taken witnesses."""
    nodes, instructions = {}, collections.defaultdict(list)
    current = None
    for line in text.splitlines():
        match = re.match(r"^([0-9a-f]+) <(.+)>:$", line)
        if match:
            current = int(match[1], 16)
            nodes[current] = {
                "name": stable_name(match[2]),
                "address": hex(current),
                "object": provenance(current),
                "edges": [],
                "indirect": [],
                "references": [],
            }
            nodes[current]["language"] = object_language(nodes[current]["object"])
            continue
        match = re.match(r"\s*([0-9a-f]+):\s+[0-9a-f]+\s+([\w.]+)\s*(.*)", line)
        if current is not None and match:
            instructions[current].append((int(match[1], 16), match[2], match[3]))
    addresses = sorted(nodes)
    ends = {
        a: max((pc + 4 for pc, _, _ in code), default=a)
        for a, code in instructions.items()
    }
    for address, code in instructions.items():
        node = nodes[address]
        for pc, op, operands in code:
            target = re.search(r"(?:#\s*)?\b([0-9a-f]{8}) <", operands)
            control = op in (
                "jal",
                "jalr",
                "j",
                "jr",
                "call",
                "tail",
                "beq",
                "bne",
                "blt",
                "bge",
                "bltu",
                "bgeu",
                "beqz",
                "bnez",
                "blez",
                "bgez",
                "bltz",
                "bgtz",
                "ble",
                "bgt",
                "bleu",
                "bgtu",
            )
            if target:
                dest = int(target[1], 16)
                if control:
                    pos = bisect.bisect_right(addresses, dest) - 1
                    if pos >= 0:
                        owner = addresses[pos]
                        # Branches within a function are not call-graph edges.
                        if dest < ends.get(owner, owner):
                            if owner != address:
                                node["edges"].append(
                                    {"target": owner, "pc": hex(pc), "kind": op}
                                )
                        else:
                            node["indirect"].append(
                                {
                                    "pc": hex(pc),
                                    "instruction": op + " " + operands,
                                    "reason": "unmapped direct target",
                                }
                            )
                    else:
                        node["indirect"].append(
                            {
                                "pc": hex(pc),
                                "instruction": op + " " + operands,
                                "reason": "unmapped direct target",
                            }
                        )
                else:
                    node["references"].append({"address": dest, "pc": hex(pc)})
            elif op == "jalr" or (op == "jr" and operands.strip() not in ("ra", "x1")):
                node["indirect"].append(
                    {"pc": hex(pc), "instruction": op + " " + operands}
                )
    annotate_instructions(nodes, instructions, symbols)
    return nodes


def high_part_states(code):
    """Must-reaching LUI/AUIPC definitions across joins and ABI calls.

    A join retains a register only when every incoming path has the same
    high value; all contributing definitions are retained. Calls invalidate caller-saved registers. This tracks relocation
    construction, not callback targets or general integer values.
    """
    positions = {pc: i for i, (pc, _, _) in enumerate(code)}
    incoming = [None] * len(code)
    if not code:
        return incoming
    incoming[0] = {}
    pending = collections.deque([0])
    while pending or any(state is None for state in incoming):
        if not pending:
            # Computed jumps can enter blocks absent from the direct CFG.
            # Start those regions with no assumed register values.
            entry = next(i for i, state in enumerate(incoming) if state is None)
            incoming[entry] = {}
            pending.append(entry)
        index = pending.popleft()
        pc, op, operands = code[index]
        state = dict(incoming[index])
        bare = operands.split(" #", 1)[0].split(" <", 1)[0].strip()
        parts = [part.strip() for part in bare.split(",")]
        conditional = op.startswith("b")
        call = op in ("jal", "jalr", "call")
        if op in ("lui", "auipc") and len(parts) == 2:
            state[parts[0]] = (
                (int(parts[1], 0) << 12) + (pc if op == "auipc" else 0),
                frozenset((index,)),
            )
        elif op == "mv" and len(parts) == 2 and parts[1] in state:
            state[parts[0]] = state[parts[1]]
        elif call:
            state = {
                r: v for r, v in state.items() if re.fullmatch(r"s\d+|sp|gp|tp", r)
            }
        elif not conditional and op not in ("sw", "sh", "sb", "j", "jr", "ret", "tail"):
            state.pop(parts[0], None)
        successors = []
        no_return = call and re.search(r"<(?:__stack_chk_fail|abort)>", operands)
        if (
            not no_return
            and op not in ("j", "jr", "ret", "tail")
            and index + 1 < len(code)
        ):
            successors.append(index + 1)
        if conditional or op == "j":
            target = re.search(r"\b([0-9a-f]{8}) <", operands)
            if target and int(target[1], 16) in positions:
                successors.append(positions[int(target[1], 16)])
        for following in successors:
            previous = incoming[following]
            merged = (
                state
                if previous is None
                else {
                    r: (value, origins | state[r][1])
                    for r, (value, origins) in previous.items()
                    if r in state and state[r][0] == value
                }
            )
            if previous != merged:
                incoming[following] = dict(merged)
                pending.append(following)
    return [state or {} for state in incoming]


def annotate_instructions(nodes, instructions, symbols=None):
    """Address-independent code contracts and local pointer-load provenance.

    This is local expression tracking, not a points-to proof. Loads preserve
    offsets (including vtable slots); calls/branches invalidate register facts.
    Duplicate sites with the same source receive separate occurrence numbers.
    Objdump comments without a verified construction are diagnostic, not proof.
    """
    for address, code in instructions.items():
        node = nodes[address]
        registers = {}
        high_states = high_part_states(code)
        normalized = []
        relocation_origins = set()
        removed = set()
        positions = {pc: index for index, (pc, _, _) in enumerate(code)}
        local_targets = {}
        unowned = {}
        sites = {site["pc"]: site for site in node["indirect"]}
        for index, (pc, op, operands) in enumerate(code):
            high_parts = high_states[index]
            bare = operands.split(" #", 1)[0].split(" <", 1)[0].strip()
            annotation = re.search(r"([0-9a-f]{8}) <(.+)>", operands)
            parts = [part.strip() for part in bare.split(",")]
            base = None
            low = None
            if op == "addi" and len(parts) == 3:
                base, low = parts[1:]
            elif op == "mv" and len(parts) == 2:
                base, low = parts[1], "0"
            elif op in ("lw", "lwu", "lh", "lhu", "lb", "lbu", "jalr"):
                match = re.fullmatch(r"(-?\d+)\((\w+)\)", parts[-1])
                if match:
                    low, base = match.groups()
                elif op == "jalr" and parts[-1] in high_parts:
                    low, base = "0", parts[-1]
            if annotation and not (
                op in ("j", "jr", "jal", "jalr", "call", "tail")
                or op.startswith("b")
                or base in high_parts
            ):
                # Objdump tracks constants linearly and can annotate a load
                # after a join with a value from the wrong predecessor.
                annotation = None
            inferred = None
            if base in high_parts and low is not None:
                value, origins = high_parts[base]
                candidate = (value + int(low, 0)) & 0xFFFFFFFF
                if annotation or (
                    symbols
                    and (symbols.resolve(candidate) or symbols.is_data(candidate))
                ):
                    inferred = candidate
                    relocation_origins.update(origins)
            symbolic = None
            if annotation or inferred is not None:
                dest = int(annotation[1], 16) if annotation else inferred
                symbolic = (
                    node_id(nodes[dest])
                    if dest in nodes
                    else symbols.resolve(dest)
                    if symbols
                    else None
                )
                if dest != address and dest in positions:
                    symbolic = f"local@{dest:x}"
                    local_targets[symbolic] = positions[dest]
                if symbolic is None:
                    if pc not in unowned:
                        unowned[pc] = {
                            "reference": f"unowned-data#{len(unowned) + 1}",
                            "address": hex(dest),
                            "annotation": annotation[2]
                            if annotation
                            else "inferred high/low address",
                            "reason": "no owning ELF symbol; identity uses consumer and reference ordinal",
                        }
                    # The code position and indirect-site occurrence identify
                    # this reference; unrelated anonymous constants must not
                    # renumber its pointer provenance.
                    symbolic = "unowned-data"
                if symbolic.startswith(node["name"] + "+"):
                    symbolic = "local" + symbolic[len(node["name"]) :]
            text = bare
            if symbolic:
                # The symbol retains offsets for interior/data references. Do
                # not include split relocation immediates or absolute addresses.
                text = ",".join(parts[:-1]) + ",ref:" + symbolic
                if op == "mv":
                    op = "addi"
                    text = ",".join(parts) + ",ref:" + symbolic
            elif op in ("auipc", "lui") and len(parts) == 2 and index + 1 < len(code):
                following = code[index + 1][2]
                if re.search(r"[0-9a-f]{8} <", following) and re.search(
                    r"\b" + re.escape(parts[0]) + r"\b", following.split(" #", 1)[0]
                ):
                    text = parts[0] + ",relocation"
            display_op = op
            if symbolic and op in ("jal", "jalr"):
                display_op = "call"
                if (
                    op == "jalr"
                    and base == "ra"
                    and index > 0
                    and code[index - 1][1] == "auipc"
                    and code[index - 1][2].startswith("ra,")
                ):
                    removed.add(index - 1)
            normalized.append(display_op + " " + text)
            site = sites.get(hex(pc))
            if site is not None:
                site["stable_instruction"] = normalized[-1]
            if site is not None and "reason" not in site:
                match = re.search(r"(-?\d+)\((\w+)\)$", bare)
                register = match[2] if match else parts[-1]
                source = registers.get(register, "register:" + register)
                if match and int(match[1]):
                    source += "+" + match[1]
                site["pointer_source"] = source
            if not parts:
                continue
            dest_reg = parts[0]
            if op == "mv" and len(parts) == 2:
                registers[dest_reg] = registers.get(parts[1], "register:" + parts[1])
            elif op in ("lw", "lwu", "lh", "lhu", "lb", "lbu") and len(parts) == 2:
                match = re.fullmatch(r"(-?\d+)\((\w+)\)", parts[1])
                if symbolic:
                    registers[dest_reg] = f"{op}[symbol:{symbolic}]"
                elif match:
                    base = registers.get(match[2], "register:" + match[2])
                    registers[dest_reg] = f"{op}[{base}+{match[1]}]"
            elif symbolic and op not in ("j", "jal", "jalr", "jr"):
                registers[dest_reg] = "symbol:" + symbolic
            elif op in ("addi", "add") and len(parts) == 3:
                base = registers.get(parts[1], "register:" + parts[1])
                registers[dest_reg] = base + "+" + parts[2]
            elif op not in ("sw", "sh", "sb", "j", "jr", "ret"):
                registers.pop(dest_reg, None)
            # Bound expression size and never carry an assumed definition over
            # a control-flow join or a call that can clobber registers.
            registers = {r: v for r, v in registers.items() if len(v) <= 256}
            if op in ("j", "jal", "jalr", "jr", "ret") or op.startswith("b"):
                registers.clear()
        for origin in relocation_origins:
            normalized[origin] = normalized[origin].split(",", 1)[0] + ",relocation"
        logical_positions = [0]
        for i in range(len(code)):
            logical_positions.append(logical_positions[-1] + (i not in removed))
        labels = {
            name: "local-instruction:" + str(logical_positions[target])
            for name, target in local_targets.items()
        }

        def replace_labels(text, labels=labels):
            return re.sub(r"local@[0-9a-f]+", lambda m: labels[m[0]], text)

        node["code"] = [
            replace_labels(text)
            for i, text in enumerate(normalized)
            if i not in removed
        ]
        for site in node["indirect"]:
            for field in ("pointer_source", "stable_instruction"):
                if field in site:
                    site[field] = replace_labels(site[field])
        node["unowned_references"] = list(unowned.values())


def decode_tables(elf, nodes, demangled, ranges=()):
    """Decode every linked data object's function pointers, including vtables."""
    tables = {}
    candidates = [
        (demangled[raw], address, size)
        for raw, address, size, kind in elf.symbols
        if kind == 1
    ]
    occupied = {address for _, address, _ in candidates}
    for start, end, obj in ranges:
        if start in occupied:
            continue
        if any(
            sec[2] & 2
            and not sec[2] & 4
            and sec[1] != 8
            and sec[3] <= start < end <= sec[3] + sec[5]
            for sec in elf.sections
        ):
            candidates.append(("anonymous table in " + obj, start, end - start))
    for name, address, size in candidates:
        if not size or size > 1024 * 1024:
            continue
        try:
            data = elf.read(address, size)
        except ValueError:
            continue
        entries = []
        for offset in range(0, len(data) - 3, 4):
            target = struct.unpack_from("<I", data, offset)[0]
            if target in nodes:
                entries.append({"offset": offset, "target": target})
        if entries:
            tables[address] = {
                "name": name,
                "address": hex(address),
                "size": size,
                "entries": entries,
            }
    return tables


def add_references(nodes, tables):
    starts = sorted(tables)
    for node in nodes.values():
        for reference in node.pop("references"):
            address = reference["address"]
            if address in nodes:
                node["edges"].append(
                    {"target": address, "pc": reference["pc"], "kind": "address-taken"}
                )
                continue
            pos = bisect.bisect_right(starts, address) - 1
            if pos < 0:
                continue
            table = tables[starts[pos]]
            if address >= starts[pos] + table["size"]:
                continue
            for entry in table["entries"]:
                node["edges"].append(
                    {
                        "target": entry["target"],
                        "pc": reference["pc"],
                        "kind": "table-reference",
                        "table": table["name"],
                        "offset": entry["offset"],
                    }
                )


def add_callback_candidates(nodes):
    """A call may invoke any callback address materialized by its caller.

    This is deliberately flow-insensitive: it covers stack-built callback structs
    and Rust trait vtables without claiming register/argument or branch precision.
    Keep unresolved jalr sites even when candidate targets are supplied.
    """
    pending = []
    for node in nodes.values():
        callbacks = [
            e
            for e in node["edges"]
            if e["kind"] in ("address-taken", "table-reference")
            and not e.get("table", "").startswith("vtable for ")
        ]
        calls = [
            e
            for e in node["edges"]
            if e["kind"] not in ("address-taken", "table-reference")
        ]
        for call in calls:
            target = call["target"]
            if nodes[target]["language"] == node["language"]:
                continue
            # Infer arguments at the firmware's xp_* ABI contracts. Other
            # consumers remain explicit unresolved sites, not guessed bindings.
            if not nodes[target]["name"].startswith("xp_"):
                continue
            for callback in callbacks:
                if callback["target"] != target:
                    pending.append(
                        (
                            target,
                            {
                                **callback,
                                "kind": "callback-candidate",
                                "supplied_by": node["name"],
                                "call_pc": call["pc"],
                            },
                        )
                    )
    for target, edge in pending:
        nodes[target]["edges"].append(edge)


def reader_roots(elf, tables, nodes, config):
    roots, decoded = {}, []
    for spec in config["root_tables"]:
        matches = [
            t for t in tables.values() if re.fullmatch(spec["symbol"], t["name"])
        ]
        if len(matches) != 1:
            raise ValueError(
                f"expected one reader table {spec['id']}, got {len(matches)}"
            )
        table = matches[0]
        # Read field names from the ABI/binding source, not a frozen slot count.
        source = (ROOT / spec["source"]).read_text()
        body = source.split(spec["start"], 1)[1].split(spec["end"], 1)[0]
        # Preprocess only conditional directives and field declarations. No headers,
        # SDK checkout, target compiler, or firmware build are needed.
        fragment = "\n".join(
            line
            for line in body.splitlines()
            if re.match(r"^#(?:if|else|elif|endif)", line)
            or re.match(spec["fields"], line)
        )
        fragment = subprocess.run(
            ["cpp", "-P", "-DXP_SD_FONT_CATALOG=1"],
            input=fragment,
            text=True,
            capture_output=True,
            check=True,
        ).stdout
        fields = re.findall(r"^\s*\.(\w+)\s*=", fragment, re.MULTILINE)
        fields = [f for f in fields if f not in spec.get("exclude", [])]
        if len(fields) * 4 != table["size"]:
            raise ValueError(
                f"{spec['id']}: source fields {len(fields)} != ELF bytes {table['size']}"
            )
        entries = {e["offset"]: e["target"] for e in table["entries"]}
        for index, field in enumerate(fields):
            target = entries.get(index * 4)
            name = f"table:{spec['id']}.{field}"
            if target is None:
                pointer = struct.unpack(
                    "<I", elf.read(int(table["address"], 16) + index * 4, 4)
                )[0]
                if field in spec.get("data_fields", []):
                    decoded.append(
                        {
                            "root": name,
                            "target": "data-context",
                            "address": hex(pointer),
                        }
                    )
                    continue
                nested = tables.get(pointer)
                if nested is None:
                    raise ValueError(
                        f"{name}: null or unresolved table target {pointer:#x}"
                    )
                decoded.append(
                    {"root": name, "target": nested["name"], "address": hex(pointer)}
                )
                for child in nested["entries"]:
                    roots[f"{name}+{child['offset']}"] = child["target"]
                continue
            roots[name] = target
            decoded.append(
                {"root": name, "target": nodes[target]["name"], "address": hex(target)}
            )
    return roots, decoded


def add_source_callbacks(nodes, roots, config):
    supplements = []
    for spec in config.get("callback_roots", []):
        for address, node in nodes.items():
            if node["language"] != spec.get("language", "native"):
                continue
            if re.fullmatch(spec["pattern"], node["name"]):
                roots["source-callback:" + node["name"]] = address
                supplements.append({"function": node["name"], "reason": spec["reason"]})
    for spec in config.get("callback_bindings", []):
        targets = [
            a for a, n in nodes.items() if re.fullmatch(spec["target"], n["name"])
        ]
        origins = [
            roots["table:" + root] for root in spec["roots"] if "table:" + root in roots
        ]
        if "from_pattern" in spec:
            origins.extend(
                a
                for a, n in nodes.items()
                if re.fullmatch(spec["from_pattern"], n["name"])
            )
        for address in origins:
            if not targets:
                raise ValueError(
                    f"callback binding has no linked targets; review {spec['source']}"
                )
            for target in targets:
                nodes[address]["edges"].append(
                    {
                        "target": target,
                        "pc": "source",
                        "kind": "source-callback",
                        "source": spec["source"],
                        "reason": spec["reason"],
                    }
                )
    return supplements


def object_id(obj):
    obj = re.sub(r"^.*?\.pio/build/[^/]+/", "build/", obj)
    obj = re.sub(r"^.*?/\.platformio/packages/", "packages/", obj)
    obj = re.sub(r"build/lib[0-9a-f]{3}/", "build/lib/", obj)
    if ".rcgu.o" in obj:
        obj = re.sub(r"([.-])[0-9a-f]{16,}(?=[.-])", r"\1HASH", obj)
        obj = re.sub(r"cgu\.\d+", "cgu.UNIT", obj)
    return obj


def node_id(node):
    return object_id(node["object"]) + " | " + stable_name(node["name"])


def digest_contract(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def site_identities(node):
    counts = collections.Counter()
    for site in node["indirect"]:
        if "reason" in site:
            continue
        kind = site["instruction"].split()[0]
        source = site.get("pointer_source", site["instruction"].split(maxsplit=1)[-1])
        key = kind + " via " + source
        counts[key] += 1
        yield {
            **site,
            "id": node_id(node) + " | " + key + f" #{counts[key]}",
            "pointer_source": source,
        }


def edge_kind(edge):
    return "call" if edge["kind"] in ("jal", "jalr") else edge["kind"]


def function_contract(node, nodes):
    # Include ALL outgoing graph edges, not merely a shortest witness. The
    # multiset retains duplicate call sites without depending on their PCs.
    edges = sorted(
        (
            edge_kind(e),
            node_id(nodes[e["target"]]),
            object_id(stable_name(e.get("table", ""))),
            e.get("offset", 0),
            e.get("supplied_by", ""),
        )
        for e in node["edges"]
    )
    return digest_contract(
        {
            "code": node.get("code", []),
            "edges": edges,
            "unknown": sorted(site["id"] for site in site_identities(node)),
            "external": sorted(
                site.get("stable_instruction", site["instruction"])
                for site in node["indirect"]
                if "reason" in site
            ),
        }
    )


def compare_uncertainty(report, baseline):
    """Gate unknown-site growth/retirement AND unchanged-site contract reuse."""
    if not baseline.get("contract_reason", "").strip():
        raise ValueError("uncertainty baseline needs a contract review reason")
    actual = {site["id"] for site in report["unresolved_indirect"]}
    if len(actual) != len(report["unresolved_indirect"]):
        raise ValueError(
            "ambiguous unresolved-site identities; retain distinct linkage aliases"
        )
    expected = baseline["sites"]
    for identity, entry in expected.items():
        if not entry.get("reason", "").strip():
            raise ValueError(f"unresolved site needs a reviewed reason: {identity}")
    errors = [
        {"kind": "new-unresolved", "site": s} for s in sorted(actual - expected.keys())
    ]
    errors += [
        {"kind": "stale-unresolved", "site": s}
        for s in sorted(expected.keys() - actual)
    ]
    old, new = baseline["contracts"], report["contracts"]
    for identity in sorted(old.keys() | new.keys()):
        if identity not in old:
            kind = "new-contract"
        elif identity not in new:
            kind = "stale-contract"
        elif old[identity] != new[identity]:
            kind = "changed-contract"
        else:
            continue
        errors.append({"kind": kind, "function": identity})
    return errors


def analyse(nodes, roots):
    """Boundary identity = Rust caller, first native entry, first Rust return.

    Enumerate endpoint triples, not infinitely many paths through native cycles.
    Preserve one shortest witness for each native entry/return pair.
    """
    reachable = set()
    todo = list(roots.values())
    while todo:
        address = todo.pop()
        if address in reachable:
            continue
        reachable.add(address)
        todo.extend(
            e["target"] for e in nodes[address]["edges"] if e["target"] in nodes
        )
    contract_groups = collections.defaultdict(list)
    for address in sorted(reachable):
        contract_groups[node_id(nodes[address])].append(
            function_contract(nodes[address], nodes)
        )
    contracts = {
        identity: digest_contract(sorted(values))
        for identity, values in sorted(contract_groups.items())
    }
    callers = collections.defaultdict(set)
    call_sites = collections.defaultdict(set)
    for name, address in roots.items():
        if nodes[address]["language"] != "rust":
            callers[address].add(name)
            call_sites[address].add(name)
    for address in sorted(reachable):
        node = nodes[address]
        if node["language"] == "rust":
            occurrences = collections.Counter()
            for edge in node["edges"]:
                target = edge["target"]
                if target in nodes and nodes[target]["language"] != "rust":
                    callers[target].add(node["name"])
                    identity = (
                        node_id(node)
                        + " | "
                        + edge_kind(edge)
                        + " -> "
                        + node_id(nodes[target])
                    )
                    occurrences[identity] += 1
                    call_sites[target].add(identity + f" #{occurrences[identity]}")
    boundaries = {}
    for entry in sorted(callers, key=lambda a: nodes[a]["name"]):
        paths = {entry: []}
        queue = collections.deque([entry])
        returns = {}
        while queue:
            address = queue.popleft()
            node = nodes[address]
            if node["language"] == "rust":
                returns[node["name"]] = paths[address]
                continue
            for edge in sorted(
                node["edges"], key=lambda e: (nodes[e["target"]]["name"], e["pc"])
            ):
                target = edge["target"]
                if target not in paths:
                    paths[target] = paths[address] + [
                        {
                            "from": node["name"],
                            "to": nodes[target]["name"],
                            **{k: v for k, v in edge.items() if k != "target"},
                        }
                    ]
                    queue.append(target)
        if returns:
            key = object_id(nodes[entry]["object"]) + " | " + nodes[entry]["name"]
            boundary = boundaries.setdefault(
                key,
                {
                    "id": key,
                    "native": nodes[entry]["name"],
                    "object": object_id(nodes[entry]["object"]),
                    "callers": set(),
                    "call_sites": set(),
                    "native_functions": set(),
                    "returns": set(),
                    "witnesses": {},
                },
            )
            boundary["callers"].update(callers[entry])
            boundary["call_sites"].update(call_sites[entry])
            boundary["native_functions"].update(
                node_id(nodes[a]) for a in paths if nodes[a]["language"] != "rust"
            )
            boundary["returns"].update(returns)
            for endpoint, path in returns.items():
                old = boundary["witnesses"].get(endpoint)
                if old is None or len(path) < len(old):
                    boundary["witnesses"][endpoint] = path
    for boundary in boundaries.values():
        boundary["callers"] = sorted(boundary["callers"])
        boundary["returns"] = sorted(boundary["returns"])
        boundary["call_sites"] = sorted(boundary["call_sites"])
        boundary["native_functions"] = sorted(boundary["native_functions"])
        boundary["native_contract"] = digest_contract(
            {identity: contracts[identity] for identity in boundary["native_functions"]}
        )
    unresolved = []
    for address in sorted(reachable):
        node = nodes[address]
        for site in [
            *site_identities(node),
            *(s for s in node["indirect"] if "reason" in s),
        ]:
            unresolved.append(
                {"function": node["name"], "object": node["object"], **site}
            )
    unknown = [
        nodes[a]["name"] for a in sorted(reachable) if nodes[a]["language"] == "unknown"
    ]
    return {
        "boundaries": sorted(boundaries.values(), key=lambda b: b["id"]),
        "unresolved_indirect": [site for site in unresolved if "reason" not in site],
        "unresolved_external": [site for site in unresolved if "reason" in site],
        "unknown_language": unknown,
        "reachable_functions": len(reachable),
        "contracts": contracts,
        "unowned_references": [
            {"function": node_id(nodes[a]), **reference}
            for a in sorted(reachable)
            for reference in nodes[a].get("unowned_references", [])
        ],
    }


def compare(report, allowlist):
    errors = []
    expected = {}
    for section in ("physical", "inherited", "deferred"):
        for entry in allowlist.get(section, []):
            native = entry["id"]
            if native in expected:
                raise ValueError(f"duplicate allowlist native entry: {native}")
            if section == "physical" and not entry.get("reason", "").strip():
                raise ValueError(f"physical entry needs a reason: {native}")
            if section != "physical" and entry.get("owner") not in OWNER_TASKS[section]:
                raise ValueError(
                    f"{section} entry needs an owner from {sorted(OWNER_TASKS[section])}: {native}"
                )
            if section == "physical" and not (
                entry.get("call_sites") and entry.get("native_contract")
            ):
                raise ValueError(
                    f"physical entry needs call sites and native contract: {native}"
                )
            for field in ("callers", "returns"):
                values = entry[field]
                if not values or len(values) != len(set(values)):
                    raise ValueError(f"empty or duplicate {field} in {native}")
            expected[native] = entry
    actual = {b["id"]: b for b in report["boundaries"]}
    for native in sorted(set(expected) | set(actual)):
        if native not in expected:
            errors.append({"kind": "new-boundary", "native": native})
        elif native not in actual:
            errors.append({"kind": "stale-boundary", "native": native})
        else:
            fields = ["callers", "returns"]
            if "native_contract" in expected[native]:
                fields.append("call_sites")
                if (
                    expected[native]["native_contract"]
                    != actual[native]["native_contract"]
                ):
                    errors.append({"kind": "changed-native-contract", "native": native})
            for field in fields:
                for value in sorted(
                    set(actual[native][field]) - set(expected[native][field])
                ):
                    errors.append(
                        {"kind": "new-" + field, "native": native, "symbol": value}
                    )
                for value in sorted(
                    set(expected[native][field]) - set(actual[native][field])
                ):
                    errors.append(
                        {"kind": "stale-" + field, "native": native, "symbol": value}
                    )
    return errors


def summarize(report, allowlist):
    actual = {b["id"] for b in report["boundaries"]}
    classes = {}
    owners = {owner: 0 for values in OWNER_TASKS.values() for owner in sorted(values)}
    classified = set()
    for section in ("physical", "inherited", "deferred"):
        entries = [e for e in allowlist.get(section, []) if e["id"] in actual]
        classes[section] = len(entries)
        classified.update(e["id"] for e in entries)
        for entry in entries:
            if section != "physical":
                owners[entry["owner"]] += 1
    classes["unclassified"] = len(actual - classified)
    return {
        "classes": classes,
        "owners": owners,
        "common_path_clean": classes["inherited"] == 0,
    }


def check_source_evidence(config):
    if "sdk_pin" in config:
        pinned = subprocess.check_output(
            ["git", "-C", str(ROOT), "ls-tree", "HEAD", "freeink-sdk"], text=True
        ).split()[2]
        if (ROOT / "freeink-sdk/.git").exists():
            local = subprocess.check_output(
                ["git", "-C", str(ROOT / "freeink-sdk"), "rev-parse", "HEAD"], text=True
            ).strip()
        else:
            local = pinned
        if pinned != config["sdk_pin"] or local != pinned:
            raise ValueError("SDK callback evidence pin changed; review roots.json")
    for path, expected in config.get("source_fingerprints", {}).items():
        actual = hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(
                f"source callback evidence changed: {path}; review roots.json bindings "
                "before updating its source fingerprint"
            )


def build_graph(elf_path, map_path, objects_path, objdump, config):
    check_source_evidence(config)
    elf = Elf(elf_path)
    ranges, inventory = map_inventory(map_path.read_text())
    if objects_path:
        supplied = set(json.loads(objects_path.read_text()))
        if supplied != set(inventory):
            raise ValueError("object/archive list differs from map LOAD records")
    if not inventory or not ranges:
        raise ValueError("map has no linked object inventory/ranges")
    starts = [r[0] for r in ranges]

    def provenance(address):
        pos = bisect.bisect_right(starts, address) - 1
        if pos >= 0 and address < ranges[pos][1]:
            return ranges[pos][2]
        return ""

    disassembly = subprocess.check_output(
        [objdump, "-d", "-C", str(elf_path)], text=True
    )
    filt = objdump.removesuffix("objdump") + "c++filt"
    raw = sorted({s[0] for s in elf.symbols})
    names = subprocess.run(
        [filt], input="\n".join(raw) + "\n", text=True, capture_output=True, check=True
    ).stdout.splitlines()
    demangled = dict(zip(raw, map(stable_name, names)))
    nodes = parse_disassembly(
        disassembly, provenance, ReferenceSymbols(elf.symbols, demangled, elf.sections)
    )
    tables = decode_tables(elf, nodes, demangled, ranges)
    add_references(nodes, tables)
    add_callback_candidates(nodes)
    roots, decoded = reader_roots(elf, tables, nodes, config)
    supplements = add_source_callbacks(nodes, roots, config)
    report = analyse(nodes, roots)
    if report["unknown_language"]:
        raise ValueError(
            "reachable functions without map provenance: "
            + str(report["unknown_language"][:10])
        )
    report["source_callback_roots"] = supplements
    report.update(
        {
            "schema": 1,
            "elf_sha256": hashlib.sha256(elf.data).hexdigest(),
            "map_sha256": hashlib.sha256(map_path.read_bytes()).hexdigest(),
            "objects": inventory,
            "root_tables": decoded,
            "decoded_function_pointer_tables": len(tables),
        }
    )
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--elf", type=pathlib.Path, default=ROOT / ".pio/build/default/firmware.elf"
    )
    parser.add_argument(
        "--map", type=pathlib.Path, default=ROOT / ".pio/build/default/firmware.map"
    )
    parser.add_argument(
        "--objects",
        type=pathlib.Path,
        help="JSON list of map LOAD inputs; defaults to the map's own list",
    )
    parser.add_argument(
        "--objdump",
        default=shutil.which("riscv32-esp-elf-objdump")
        or str(
            pathlib.Path.home()
            / ".platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-objdump"
        ),
    )
    parser.add_argument(
        "--report", type=pathlib.Path, default=ROOT / ".pio/reader-chain-report.json"
    )
    args = parser.parse_args()
    start = time.monotonic()
    try:
        report = build_graph(
            args.elf,
            args.map,
            args.objects,
            args.objdump,
            json.loads((CONFIG / "roots.json").read_text()),
        )
        report["physical_notes"] = json.loads((CONFIG / "allowlist.json").read_text())[
            "physical"
        ]
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        print(
            f"{len(report['boundaries'])} native boundaries; "
            f"{len(report['unresolved_indirect'])} unresolved indirect sites; "
            f"{len(report['unresolved_external'])} external direct sites; "
            f"audit only; {time.monotonic() - start:.2f}s"
        )
        print(f"Report: {args.report}")
        return 0
    except (
        ValueError,
        OSError,
        subprocess.SubprocessError,
        KeyError,
        IndexError,
        struct.error,
    ) as error:
        print(f"reader chain guard: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
