#!/usr/bin/env python3
"""Check native section references below Rust ports, without linking or disassembly."""

import argparse
import json
import re
import shutil
import subprocess
import time
from collections import Counter, defaultdict, deque
from functools import lru_cache
from pathlib import Path

HERE = Path(__file__).resolve().parent
OWNERS = {
    "inherited": {
        "R1",
        "R1b",
        "R1c",
        "R2",
        "R2b",
        "R3a",
        "R3b",
        "R4",
        "R-RELAY",
        "R-RUNTIME",
    },
    "deferred": {
        "R5", "R6", "R7", "NONREADER", "RUNTIME-SHOT", "RUNTIME-SLEEP", "SET-OWN", "THEME-FOOTER"
    },
}


def policy(path):
    result = defaultdict(dict)
    for number, line in enumerate(path.read_text().splitlines(), 1):
        value, _, reason = line.partition("#")
        words = value.split()
        if not words:
            continue
        kind, key, *rest = words
        if (
            kind
            not in {
                "table",
                "physical",
                "callback",
                "import",
                "patch",
                "dispatch",
                "rustfill",
                *OWNERS,
            }
            or key in result[kind]
        ):
            raise ValueError(f"{path}:{number}: unknown or duplicate policy entry")
        if kind in OWNERS and (len(rest) != 1 or rest[0] not in OWNERS[kind]):
            raise ValueError(f"{path}:{number}: unknown owner")
        if not reason.strip():
            raise ValueError(f"{path}:{number}: missing reason")
        result[kind][key] = rest
    return result


def rust_symbols(text, native_runtime=frozenset()):
    defined, undefined = set(), set()
    builtins = False
    for line in text.splitlines():
        if line.endswith("]:"):
            builtins = "[compiler_builtins-" in line
        fields = line.split()
        if len(fields) > 1 and len(fields[1]) == 1:
            name, kind = fields[:2]
            # build_rust.py orders libgcc/libm/ROM before Rust's weak fallbacks.
            if builtins and kind in "WV" and name in native_runtime:
                continue
            (undefined if kind in "Uwv" else defined).add(name)
    return defined, undefined - defined


def objects(lines, build_dir=None):
    """GNU readelf -SW -sW -rW; retain allocated sections, not debug references."""
    graph, definitions, symbols, tables, functions = {}, defaultdict(set), {}, {}, set()
    local_data = set()
    obj, sections, relocs, syms, section = "", {}, defaultdict(list), {}, None

    def finish():
        for index, (name, size) in sections.items():
            node = (obj, name)
            graph[node], tables[node] = set(), []
            for offset, symbol, kind in relocs[index]:
                if symbol not in syms:
                    raise ValueError(
                        f"{obj}:{name}: missing relocation symbol {symbol}"
                    )
                target, target_index, _, _, binding, _ = syms[symbol]
                destination = (
                    (obj, sections[target_index][0])
                    if binding == "LOCAL" and target_index in sections
                    else target
                )
                graph[node].add(destination)
                tables[node].append((offset, destination, kind))
        for name, index, value, size, binding, kind in syms.values():
            if index in sections:
                node = (obj, sections[index][0])
                symbols[(obj, name)] = (node, value, size)
                if binding == "LOCAL" and size and not node[1].startswith(".text"):
                    local_data.add((obj, name))
                if binding in {"GLOBAL", "WEAK", "UNIQUE"}:
                    definitions[name].add(node)
                    if kind == "FUNC":
                        functions.add(name)

    for line in lines:
        if line[:1].isdigit() and section is None:
            continue
        if line.startswith("File: "):
            finish()
            obj, sections, relocs, syms, section = (
                line[6:].strip(),
                {},
                defaultdict(list),
                {},
                None,
            )
            if build_dir is not None:
                obj = str(Path(obj).relative_to(build_dir))
        elif match := re.match(
            r"\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+\S+\s+\S+\s+(\S+)\s+\S+\s+(\S+)", line
        ):
            index, name, size, flags = match.groups()
            if name.startswith(".gnu.lto_"):
                raise ValueError(f"{obj}: native LTO objects are not supported")
            if "A" in flags:
                sections[int(index)] = (name, int(size, 16))
        elif line.startswith("Relocation section "):
            name = line.split("'")[1].removeprefix(".rela")
            section = next((i for i, (n, _) in sections.items() if n == name), None)
        elif match := re.match(
            r"\s*(\d+):\s+([0-9a-f]+)\s+(0x[0-9a-f]+|\d+)\s+(\S+)\s+(\S+)\s+\S+\s+(\S+)\s+(\S+)",
            line,
        ):
            number, value, size, kind, binding, index, name = match.groups()
            syms[int(number)] = (
                name,
                int(index) if index.isdigit() else index,
                int(value, 16),
                int(size, 16) if size.startswith("0x") else int(size),
                binding,
                kind,
            )
        elif section is not None and (
            match := re.match(r"([0-9a-f]+)\s+([0-9a-f]+)\s+(\S+)", line)
        ):
            offset, info, kind = match.groups()
            symbol = int(info, 16) >> 8
            if symbol:  # R_RISCV_RELAX / ALIGN have no target.
                relocs[section].append((int(offset, 16), symbol, kind))
    finish()
    return graph, definitions, symbols, tables, functions, local_data


@lru_cache(maxsize=64)
def callback_types(text):
    names = set(re.findall(r"typedef[^;{}]*\(\s*\*(\w+)\s*\)[^;{}]*;", text))
    structures = [
        (body, alias or tag)
        for tag, body, alias in re.findall(
            r"(?:typedef\s+)?struct(?:\s+(\w+))?\s*\{([^{}]*)\}\s*(\w+)?\s*;", text
        )
        if alias or tag
    ]
    while True:
        previous = len(names)
        for body, name in structures:
            if re.search(r"\(\s*\*", body) or names.intersection(
                re.findall(r"\w+", body)
            ):
                names.add(name)
        if len(names) == previous:
            return names


def takes_callback(declaration, types):
    return bool(
        re.search(r"\(\s*\*", declaration)
        or types.intersection(re.findall(r"\w+", declaration))
    )


@lru_cache(maxsize=128)
def fields(text, struct):
    match = re.search(
        r"typedef\s+struct(?:\s+\w+)?\s*\{([^{}]*)\}\s*" + struct + r"\s*;", text
    )
    if not match:
        raise ValueError(f"cannot find preprocessed {struct}")
    aliases = {
        m[1]: m[0].removeprefix("typedef")
        for m in re.finditer(r"typedef[^;{}]*\(\s*\*(\w+)\s*\)[^;{}]*;", text)
    }
    callbacks = callback_types(text) - {struct}
    result = []
    for declaration in match[1].split(";"):
        if not declaration.strip():
            continue
        alias = re.fullmatch(r"\s*(\w+)\s+(\w+)\s*", declaration)
        if alias and alias[1] in aliases:
            declaration = aliases[alias[1]].replace(alias[1], alias[2], 1)
        pointers = re.findall(r"\(\s*\*(\w*)\s*\)", declaration)
        if pointers:
            name = pointers[0]
        else:
            data = re.fullmatch(
                r"\s*(?:const\s+)?\w+\s*(?:\*\s*(?:const\s*)?)+(\w+)\s*", declaration
            )
            if not data:
                raise ValueError(f"{struct}: non-pointer field: {declaration.strip()}")
            name = data[1]
        parameters = re.split(r"\(\s*\*\w+\s*\)", declaration, maxsplit=1)
        callback = len(parameters) == 2 and takes_callback(parameters[1], callbacks)
        result.append((name, callback))
    return result


def select_functions(pattern, symbols, definitions, roots):
    if pattern.startswith("root:"):
        selected = [
            target
            for root, targets in roots.items()
            if re.fullmatch(pattern[5:], root)
            for target in targets
        ]
        nodes = set()
        for target in selected:
            nodes.update(
                {target} if isinstance(target, tuple) else definitions.get(target, ())
            )
    else:
        nodes = {
            node
            for (_, name), (node, _, size) in symbols.items()
            if size and node[1].startswith(".text") and re.fullmatch(pattern, name)
        }
    if not nodes:
        raise ValueError(f"manual edge: no native function matches {pattern}")
    return nodes


def dynamic_edges(native, exports, config, headers, roots):
    graph, definitions, symbols, tables, _, _ = native
    function_symbols = {
        key: value
        for key, value in symbols.items()
        if value[2] and value[0][1].startswith(".text")
    }
    data_symbols = [
        (key[1], value)
        for key, value in symbols.items()
        if value[2] and not value[0][1].startswith(".text")
    ]
    for name, values in config["dispatch"].items():
        if len(values) not in (2, 3):
            raise ValueError(f"dispatch {name}: expected caller target [vtable-method]")
        caller, target, *method = values
        callers = select_functions(caller, function_symbols, definitions, roots)
        if method:
            matches = [
                (symbol, value)
                for symbol, value in data_symbols
                if re.fullmatch(target, symbol)
            ]
            if not matches:
                raise ValueError(f"dispatch {name}: missing vtable {target}")
            targets = set()
            for symbol, (node, start, size) in matches:
                slots = {
                    dest
                    for offset, dest, _ in tables[node]
                    if start <= offset < start + size
                    and re.fullmatch(
                        method[0],
                        dest[1].removeprefix(".text.")
                        if isinstance(dest, tuple)
                        else dest,
                    )
                }
                if not slots:
                    raise ValueError(
                        f"dispatch {name}: no named method {method[0]} in {symbol}"
                    )
                targets.update(slots)
        else:
            targets = select_functions(target, function_symbols, definitions, roots)
        for node in callers:
            graph[node].update(targets)
    for slot, values in config["rustfill"].items():
        if len(values) != 2 or "." not in slot:
            raise ValueError(f"rustfill {slot}: expected Struct.field caller header")
        caller, header = values
        struct, field = slot.split(".", 1)
        if field not in dict(fields(headers[header], struct)):
            raise ValueError(f"rustfill {slot}: missing header field")
        exports.add(slot)
        for node in select_functions(caller, function_symbols, definitions, roots):
            graph[node].add(slot)
    for slot, (_, header) in config["rustfill"].items():
        struct = slot.split(".", 1)[0]
        missing = {
            f"{struct}.{field}" for field, _ in fields(headers[header], struct)
        } - config["rustfill"].keys()
        if missing:
            raise ValueError(
                f"Rust-filled table needs caller contracts for every slot: {sorted(missing)}"
            )


def dynamic_inventory(native, exports, config=None, headers=None):
    """Ratchet named function slots, including tables outside the port closure."""
    graph, definitions, symbols, tables, functions, local_data = native
    named_fields = {}
    if config:
        for label, (pattern, struct, header) in config["table"].items():
            matches = [key for key in symbols if re.fullmatch(pattern, key[1])]
            if len(matches) != 1:
                raise ValueError(f"{label}: expected one table, found {len(matches)}")
            key = matches[0]
            names = [name for name, _ in fields(headers[header], struct)]
            if symbols[key][2] != 4 * len(names):
                raise ValueError(f"{label}: table size/header field mismatch")
            if key in named_fields:
                raise ValueError(f"{label}: multiple field contracts for one table")
            named_fields[key] = names
    reverse = defaultdict(set)
    for source, targets in [*graph.items(), *definitions.items()]:
        for target in targets:
            reverse[target].add(source)
    hot, queue = set(exports), deque(exports)
    while queue:
        for source in reverse[queue.popleft()] - hot:
            hot.add(source)
            queue.append(source)
    slots, all_vtables, local_owners = set(), set(), {}
    for (obj, name), (node, start, size) in symbols.items():
        identity = name
        if (obj, name) in local_data:
            # Library hash directories and absolute build roots are incidental.
            source = re.sub(r"^lib[0-9a-f]+/", "", obj)
            identity += "@" + source
            if identity in local_owners and local_owners[identity] != obj:
                raise ValueError(f"ambiguous local table identity: {identity}")
            local_owners[identity] = obj
        if name.startswith("_ZTV"):
            all_vtables.add(identity)
        if not size or node[1].startswith(".text"):
            continue
        for offset, target, kind in tables[node]:
            if not start <= offset < start + size or target not in hot:
                continue
            function = (
                target[1].startswith(".text")
                if isinstance(target, tuple)
                else target in functions or target in exports
            )
            if function:
                if kind != "R_RISCV_32" or (offset - start) % 4:
                    raise ValueError(
                        f"{name}: unsupported function-table relocation {kind}"
                    )
                index = (offset - start) // 4
                field = (
                    named_fields[(obj, name)][index]
                    if (obj, name) in named_fields
                    else str(index)
                )
                slots.add((identity, field))
    if config:
        for slot, (_, header) in config["rustfill"].items():
            struct, field = slot.split(".", 1)
            if field not in dict(fields(headers[header], struct)):
                raise ValueError(f"rustfill {slot}: missing header field")
            slots.add((struct, field))
    return slots, {
        "vtables": len(all_vtables),
        "hot_vtables": len({n for n, _ in slots if n.startswith("_ZTV")}),
        "hot_tables": len({n for n, _ in slots if not n.startswith("_ZTV")}),
    }


def compare_dynamic(actual, baseline):
    return [
        f"new dynamic slot {' '.join(slot)}: review its callers and update dispatch/rustfill contracts"
        for slot in sorted(actual - baseline)
    ] + [
        f"stale dynamic slot {' '.join(slot)}: review removal and regenerate"
        for slot in sorted(baseline - actual)
    ]


def analyse(native, rust, config, headers):
    graph, definitions, symbols, tables, functions, _ = native
    exports, imports = rust
    exports = set(exports)
    roots = {name: {name} for name in imports & definitions.keys()}
    errors, groups = (
        [],
        {
            "direct": set(roots),
            "direct_functions": set(roots) & functions,
            "direct_data": set(roots) - functions,
        },
    )
    for root in sorted(roots.keys() - config["import"].keys()):
        errors.append(
            f"{root}: new direct import requires a reviewed import contract, including callback parameters"
        )
    for root in sorted(config["import"].keys() - roots.keys()):
        errors.append(f"{root}: stale direct import contract")
    declarations = "\n".join(dict.fromkeys(headers.values()))
    types = callback_types(declarations)
    for root in groups["direct_functions"]:
        signatures = re.findall(
            r"\b" + re.escape(root) + r"\s*\(([^;{}]*)\)\s*;", declarations
        )
        if (
            any(takes_callback(signature, types) for signature in signatures)
            and root not in config["callback"]
        ):
            errors.append(
                f"{root}: direct import with callback parameters missing policy/reason"
            )
    pending_patches = set(config["patch"])
    for label, (pattern, struct, header) in config["table"].items():
        matches = [
            (obj, name, value)
            for (obj, name), value in symbols.items()
            if re.fullmatch(pattern, name)
        ]
        if len(matches) != 1:
            raise ValueError(f"{label}: expected one table, found {len(matches)}")
        _obj, _name, (node, start, size) = matches[0]
        slots = fields(headers[header], struct)
        records = [r for r in tables[node] if start <= r[0] < start + size]
        patched = {}
        for index, (field, _) in enumerate(slots):
            root = f"{label}.{field}"
            if root in config["patch"]:
                values = config["patch"][root]
                if len(values) != 1:
                    raise ValueError(f"patch {root}: expected one native function")
                targets = select_functions(values[0], symbols, definitions, roots)
                if len(targets) != 1:
                    raise ValueError(f"patch {root}: expected one native function")
                patched[start + 4 * index] = targets
                pending_patches.remove(root)
        if (
            size != 4 * len(slots)
            or sorted(
                [r[0] - start for r in records] + [offset - start for offset in patched]
            )
            != list(range(0, size, 4))
            or any(r[2] != "R_RISCV_32" for r in records)
        ):
            raise ValueError(
                f"{label}: table size/slot/relocation mismatch: {size} bytes, {len(slots)} slots, {len(records)} relocations"
            )
        groups[label] = set()
        starts = {offset: {target} for offset, target, _ in records} | patched
        for index, (field, callback) in enumerate(slots):
            root = f"{label}.{field}"
            roots[root] = starts[start + 4 * index]
            groups[label].add(root)
            if callback and root not in config["callback"]:
                errors.append(f"{root}: callback-taking slot missing policy/reason")
    if pending_patches:
        raise ValueError(f"stale patched slots: {sorted(pending_patches)}")
    errors += [
        f"{root}: stale callback contract"
        for root in sorted(config["callback"].keys() - roots.keys())
    ]
    dynamic_edges(native, exports, config, headers, roots)
    paths = {}
    for root, starts in sorted(roots.items()):
        queue = deque((node, []) for node in sorted(starts, key=str))
        seen = set()
        while queue:
            node, path = queue.popleft()
            if node in seen:
                continue
            seen.add(node)
            label = node[1] if isinstance(node, tuple) else node
            path = path + [label]
            if isinstance(node, str):
                if node in exports:
                    paths[(root, node)] = path
                targets = definitions.get(node, ())
            else:
                targets = graph[node]
            queue.extend((target, path) for target in sorted(targets, key=str))
    return paths, groups, errors


def compare(paths, lock, config):
    errors = []
    for pair in sorted(paths.keys() - lock):
        errors.append(
            f"new {pair[0]} -> {pair[1]}\n  path: "
            + " -> ".join(paths[pair])
            + "\n  fix: call the Rust owner directly; review any deliberate policy/lock addition"
        )
    for root, symbol in sorted(lock - paths.keys()):
        errors.append(
            f"stale {root} -> {symbol}: regenerate the lock after reviewing the removed path"
        )
    owners = Counter()
    classes = Counter()
    used_owners = set()
    for root, symbol in paths:
        if symbol in config["physical"].get(root, []):
            classes["physical"] += 1
            continue
        matches = [
            (kind, entries[root][0])
            for kind, entries in config.items()
            if kind in OWNERS and root in entries
        ]
        if len(matches) != 1:
            errors.append(
                f"{root}: Rust re-entry has no unique inherited/deferred owner"
            )
            classes["unowned"] += 1
        else:
            kind, owner = matches[0]
            used_owners.add(root)
            owners[owner] += 1
            classes[kind] += 1
    for kind in OWNERS:
        errors += [
            f"{root}: stale {kind} owner"
            for root in sorted(config[kind].keys() - used_owners)
        ]
    for root, symbols in config["physical"].items():
        errors += [
            f"{root} -> {symbol}: stale physical contract"
            for symbol in symbols
            if (root, symbol) not in paths
        ]
    summary = (
        f"classes={dict(sorted(classes.items()))} owners={dict(sorted(owners.items()))}; common path clean: "
        + ("no" if classes["inherited"] or classes["unowned"] else "yes")
    )
    return errors, summary


def object_files(build):
    return sorted(
        p for p in build.rglob("*.o") if p.relative_to(build).parts[0] != "rust"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir", type=Path, default=HERE.parent / ".pio/build/default"
    )
    parser.add_argument("--source", type=Path, default=HERE.parent)
    parser.add_argument(
        "--tool-prefix",
        default=(
            shutil.which("riscv32-esp-elf-nm")
            or str(
                Path.home()
                / ".platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-nm"
            )
        ).removesuffix("nm"),
        help="path ending in riscv32-esp-elf-",
    )
    parser.add_argument("--policy", type=Path, default=HERE / "rust_ports/policy.txt")
    parser.add_argument("--lock", type=Path, default=HERE / "rust_ports/ports.lock")
    parser.add_argument(
        "--report", action="store_true", help="print failures but exit zero"
    )
    parser.add_argument(
        "--write-lock",
        type=Path,
        help="write a candidate, never silently update the checked-in lock",
    )
    parser.add_argument(
        "--json", type=Path, help="write paths and inventory for review"
    )
    parser.add_argument(
        "--dynamic-lock", type=Path, default=HERE / "rust_ports/dynamic.lock"
    )
    parser.add_argument("--write-dynamic-lock", type=Path)
    args = parser.parse_args()
    started = time.monotonic()
    try:
        config = policy(args.policy)
        files = object_files(args.build_dir)
        archives = list(args.build_dir.glob("rust/*/release/firmware/libxp_ffi.a"))
        if not files or len(archives) != 1:
            raise ValueError(
                "expected native objects and one default-X3 linked Rust archive"
            )
        for path in files:
            with path.open("rb") as stream:
                prefix = stream.read(20)
            if (
                prefix[:6] != b"\x7fELF\x01\x01"
                or int.from_bytes(prefix[18:20], "little") != 243
            ):
                raise ValueError(
                    f"{path}: expected an ELF32 little-endian RISC-V native object"
                )
        # Match the default C3 multilib used before Rust in build_rust.py.
        runtime = [
            subprocess.check_output(
                [
                    args.tool_prefix + "gcc",
                    "-march=rv32imc_zicsr_zifencei",
                    "-mabi=ilp32",
                    "-fno-rtti",
                    "-print-file-name=" + library,
                ],
                text=True,
            ).strip()
            for library in ("libgcc.a", "libm.a", "libc.a")
        ]
        native_names = subprocess.check_output(
            [args.tool_prefix + "nm", "-g", "--defined-only", "-P", *runtime],
            text=True,
        )
        native_runtime = {
            words[0]
            for line in native_names.splitlines()
            if len(words := line.split()) > 1 and words[1] not in "UVWwv"
        }
        rust = rust_symbols(
            subprocess.check_output(
                [args.tool_prefix + "nm", "-g", "-P", str(archives[0])],
                text=True,
            ),
            native_runtime,
        )
        with subprocess.Popen(
            [args.tool_prefix + "readelf", "-SW", "-sW", "-rW", *map(str, files)],
            stdout=subprocess.PIPE,
            text=True,
        ) as process:
            native = objects(process.stdout, args.build_dir)
            if process.wait():
                raise ValueError("readelf failed")
        headers = {}
        required_headers = {header for _, _, header in config["table"].values()}
        required_headers.update(value[1] for value in config["rustfill"].values())
        required_headers.update(
            str(path.relative_to(args.source))
            for path in (args.source / "rust/crates/xp-ffi/include").glob("*.h")
        )
        combined = subprocess.check_output(
            [
                args.tool_prefix + "g++",
                "-E",
                "-P",
                "-x",
                "c++",
                "-DXP_SD_FONT_CATALOG",
                *[
                    part
                    for header in sorted(required_headers)
                    for part in ("-include", str(args.source / header))
                ],
                "-",
            ],
            input="",
            text=True,
        )
        headers = dict.fromkeys(required_headers, combined)
        paths, groups, errors = analyse(native, rust, config, headers)
        dynamic, dynamic_counts = dynamic_inventory(
            native, rust[0] | set(config["rustfill"]), config, headers
        )
        dynamic_lock = {
            tuple(line.split())
            for line in args.dynamic_lock.read_text().splitlines()
            if line.strip()
        }
        errors += compare_dynamic(dynamic, dynamic_lock)
        lock = {
            tuple(line.split())
            for line in args.lock.read_text().splitlines()
            if line.strip() and not line.startswith("#")
        }
        differences, summary = compare(paths, lock, config)
        errors += differences
        if args.write_lock:
            args.write_lock.write_text(
                "".join(f"{root} {symbol}\n" for root, symbol in sorted(paths))
            )
        if args.write_dynamic_lock:
            args.write_dynamic_lock.write_text(
                "".join(" ".join(slot) + "\n" for slot in sorted(dynamic))
            )
        inventory = {
            group: {
                "roots": len(roots),
                "reaching_rust": len(roots & {r for r, _ in paths}),
                "pairs": sum(r in roots for r, _ in paths),
            }
            for group, roots in groups.items()
        }
        if args.json:
            args.json.write_text(
                json.dumps(
                    {
                        "inventory": inventory,
                        "paths": {f"{r} {s}": p for (r, s), p in sorted(paths.items())},
                        "errors": errors,
                        "summary": summary,
                        "dynamic": dynamic_counts,
                    },
                    indent=2,
                )
                + "\n"
            )
        print("rust-ports:", inventory)
        print("rust-ports:", summary)
        print("rust-ports: dynamic", dynamic_counts, f"{len(dynamic)} slots")
        for error in errors:
            print("rust-ports:", error)
        print(
            f"rust-ports: {len(errors)} errors; {time.monotonic() - started:.2f}s"
            + (" [REPORT ONLY]" if args.report else "")
        )
        return int(bool(errors) and not args.report)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"rust-ports: {error}" + (" [REPORT ONLY]" if args.report else ""))
        return int(not args.report)


if __name__ == "__main__":
    raise SystemExit(main())
