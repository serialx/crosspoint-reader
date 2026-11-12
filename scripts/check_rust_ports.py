#!/usr/bin/env python3
"""Check native section references below Rust ports, without linking or disassembly."""

import argparse
import json
import re
import shutil
import subprocess
import time
from collections import Counter, defaultdict, deque
from pathlib import Path

HERE = Path(__file__).resolve().parent
OWNERS = {
    "inherited": {"R1", "R1b", "R2", "R3a", "R3b", "R4", "R-RELAY"},
    "deferred": {"R5", "R6", "R7"},
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
            kind not in {"table", "physical", "callback", *OWNERS}
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


def objects(lines):
    """GNU readelf -SW -sW -rW; retain allocated sections, not debug references."""
    graph, definitions, symbols, tables, functions = {}, defaultdict(set), {}, {}, set()
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
        elif match := re.match(
            r"\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+\S+\s+\S+\s+(\S+)\s+\S+\s+(\S+)", line
        ):
            index, name, size, flags = match.groups()
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
    return graph, definitions, symbols, tables, functions


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
    callbacks = set(aliases)
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
        callback = len(pointers) > 1 or bool(
            callbacks.intersection(re.findall(r"\w+", declaration))
        )
        result.append((name, callback))
    return result


def analyse(native, rust, config, headers):
    graph, definitions, symbols, tables, functions = native
    exports, imports = rust
    roots = {name: {name} for name in imports & definitions.keys()}
    errors, groups = (
        [],
        {
            "direct": set(roots),
            "direct_functions": set(roots) & functions,
            "direct_data": set(roots) - functions,
        },
    )
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
        if (
            size != 4 * len(slots)
            or sorted(r[0] - start for r in records) != list(range(0, size, 4))
            or any(r[2] != "R_RISCV_32" for r in records)
        ):
            raise ValueError(
                f"{label}: table size/slot/relocation mismatch: {size} bytes, {len(slots)} slots, {len(records)} relocations"
            )
        groups[label] = set()
        for (field, callback), (_, target, _) in zip(slots, sorted(records)):
            root = f"{label}.{field}"
            roots[root] = {target}
            groups[label].add(root)
            if callback and root not in config["callback"]:
                errors.append(f"{root}: callback-taking slot missing policy/reason")
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
            owners[owner] += 1
            classes[kind] += 1
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
            native = objects(process.stdout)
            if process.wait():
                raise ValueError("readelf failed")
        headers = {}
        for _, _, header in config["table"].values():
            if header not in headers:
                headers[header] = subprocess.check_output(
                    [
                        args.tool_prefix + "gcc",
                        "-E",
                        "-P",
                        "-x",
                        "c",
                        "-DXP_SD_FONT_CATALOG",
                        "-include",
                        str(args.source / header),
                        "-",
                    ],
                    input="",
                    text=True,
                )
        paths, groups, errors = analyse(native, rust, config, headers)
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
                    },
                    indent=2,
                )
                + "\n"
            )
        print("rust-ports:", inventory)
        print("rust-ports:", summary)
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
