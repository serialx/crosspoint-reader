import json
import re
import shutil
import subprocess
import sys
from functools import cache
from pathlib import Path

sys.dont_write_bytecode = True
import argparse
import gzip
import hashlib

import linked
from dictionary_rebind import rebind
from plugin_bindings import bind_file_get

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"


class Config:
    def __init__(self, prefix):
        self.build_path = HERE.parents[1]
        self.data = {
            "linked": json.loads((DATA / "x3.json").read_text()),
            "toolchain": {"c3_prefix": prefix},
        }
        self.data["linked"]["env_arch"] = {"default": "c3"}

    def raw(self, key, default=None):
        section, option = key.split(".")
        return self.data[section].get(option, default)


if not __debug__:
    raise SystemExit("Run the audit with Python assertions enabled")
args = argparse.ArgumentParser(description="Audit the linked X3 default ELF")
args.add_argument("--elf", required=True, type=Path)
args.add_argument("--out", required=True, type=Path)
args.add_argument("--tool-prefix", default="riscv32-esp-elf-")
options = args.parse_args()
side = "candidate"
elf = options.elf.resolve()
out = options.out.resolve()
work = out / "stack" / side / "default"
work.mkdir(parents=True, exist_ok=True)
cfg = Config(options.tool_prefix)
for name, expected in {
    "routes-candidate.json": "2e80d63347eeb9d8f34e543ab460563cd57ded96ed50540db7e9eb18e7df77a8",
    "attestations-candidate.json": "ce3cb32b1e9a0f49bcaf842fddf025f19f4ca9977b6cc78f73da656d53a37342",
}.items():
    content = gzip.decompress((DATA / (name + ".gz")).read_bytes())
    assert hashlib.sha256(content).hexdigest() == expected
    (work / name).write_bytes(content)
source = work / "routes-candidate.json"
cfg.data["linked"].update(
    leaf_header=str(DATA / "leaf-offsets.h"),
    routes_source={"default": str(source)},
    attestations_source={"default": str(work / "attestations-candidate.json")},
)
candidate = work / "firmware.elf"
inspection_file = work / "inspection.json"
shutil.copy2(elf, candidate)
linked.inspect(cfg, "default", work)
inspection = json.loads(inspection_file.read_text())
leaves = linked.map_leaves(cfg, work, 141)
calls = linked.resolved_calls(work)
by_name = {row["name"]: row for row in calls}
spec = json.loads(source.read_text())
font_old = "xp_core::font::loaded::ensure::<<xp_ffi::font_registry::Owner>::ensure::{closure#0}>"
font_new = "<xp_ffi::font_registry::Owner>::ensure"
error_name = "std::__throw_length_error(char const*)"
layout_name = "<xp_ui::reader_chrome::hits::Layout>::hit"
outline_route = "o5a-complete-child-input-1574-prologue"
proof = dict(elf_sha256=inspection["elf_sha256"], font={}, http={}, bookmark={})

tool = linked._prefix(cfg, "default") + "objdump"


@cache
def call_edge(caller, callee):
    function = by_name[caller]
    edge = next(edge for edge in function["calls"] if edge["name"] == callee)
    pc = int(edge["pc"], 16)
    excerpt = subprocess.check_output(
        [tool, "-d", f"--start-address={pc - 4:#x}", f"--stop-address={pc + 4:#x}", str(candidate)],
        text=True,
    )
    return dict(
        caller=caller,
        caller_address=function["address"],
        caller_frame=function["frame"],
        callee=edge,
        call_disassembly=excerpt,
    )


proof["file handles"] = {}
proof["inlined inflate"] = {}
inflate = (
    "miniz_oxide::inflate::core::decompress_with_limit::<miniz_oxide::inflate::core::tables::SmallTables>"
)
scratch_drop = "core::ptr::drop_glue::<xp_core::epub::cache::Scratch>"
vec_drop = "core::ptr::drop_glue::<alloc::vec::Vec<u8>>"
proof["scratch drop"] = {}
if scratch_drop not in by_name:
    body = (
        (work / "all-disassembly.txt").read_text().split("<" + vec_drop + ">:\n", 1)[1].split("\n\n", 1)[0]
    )
    (tail,) = [line for line in body.splitlines() if "<heap_caps_free>" in line]
    assert "jr\t" in tail and "addi\tsp,sp,16" in body
    for name, row in spec["routes"].items():
        steps = row["segments"]
        if scratch_drop in steps:
            i = steps.index(scratch_drop)
            assert steps[i + 1] == "heap_caps_free"
            proof["scratch drop"][name] = dict(
                incoming=call_edge(steps[i - 1], vec_drop),
                tail=tail,
                target_frame=by_name["heap_caps_free"]["frame"],
            )
            steps[i] = vec_drop
for name, row in spec["routes"].items():
    steps = row["segments"]
    if inflate in steps and inflate not in by_name:
        i = steps.index(inflate)
        caller, callee = steps[i - 1], steps[i + 1]
        assert caller.endswith("::step_with_limit") and "::init_tree::" in callee
        exact = call_edge(caller, callee)
        exact["all_call_sites"] = [edge for edge in by_name[caller]["calls"] if edge["name"] == callee]
        proof["inlined inflate"][name] = exact
        del steps[i]

for name, row in spec["routes"].items():
    steps = row["segments"]
    for i, step in enumerate(steps):
        if "std::make_unique<HalFile::Impl, FsFile>" in step and step not in by_name:
            assert steps[i + 1] == "operator new(unsigned int)"
            proof["file handles"][name] = call_edge(steps[i - 1], steps[i + 1])
            del steps[i]
            break
    move = "HalFile::operator=(HalFile&&)"
    reset = (
        "std::__uniq_ptr_impl<HalFile::Impl, std::default_delete<HalFile::Impl> >::reset(HalFile::Impl*)"
    )
    if move in steps and reset in by_name:
        i = steps.index(move)
        if i + 1 < len(steps) and steps[i + 1].startswith("std::default_delete<HalFile::Impl>"):
            deleter = steps[i + 1]
            line = next(
                l
                for l in (work / "all-disassembly.txt")
                .read_text()
                .split("<" + reset + ">:\n", 1)[1]
                .split("\n\n", 1)[0]
                .splitlines()
                if "<" + deleter + ">" in l
            )
            assert by_name[reset]["frame"] == 0
            proof["file handles"][name] = dict(move=call_edge(move, reset), tail=line)
            steps.insert(i + 1, reset)
    if font_old in steps and font_old not in by_name:
        i = steps.index(font_old)
        caller, callee = steps[i - 1], steps[i + 1]
        assert by_name[font_new]["frame"] == 16
        proof["font"][name] = [call_edge(caller, font_new), call_edge(font_new, callee)]
        steps[i] = font_new
    for i, (caller, callee) in enumerate(zip(steps, steps[1:])):
        if not name.startswith("http-") or "::_M_realloc_append<" not in caller or callee != error_name:
            continue
        direct = [edge for edge in by_name[caller]["calls"] if edge["name"] == callee]
        if direct:
            proof["http"][name] = [call_edge(caller, callee)]
        else:
            (helper,) = [
                edge
                for edge in by_name[caller]["calls"]
                if "::_M_check_len(" in edge["name"]
                and any(inner["name"] == callee for inner in by_name[edge["name"]]["calls"])
            ]
            assert helper["frame"] == 16, helper
            proof["http"][name] = [call_edge(caller, helper["name"]), call_edge(helper["name"], callee)]
            steps.insert(i + 1, helper["name"])
        break


def outline(folder, calls):
    layout = next(row for row in calls if row["name"] == layout_name)
    helper = layout["calls"][0]
    assert (
        layout["frame"] == 112 and helper["frame"] == 0 and helper["name"].startswith("OUTLINED_FUNCTION_")
    )
    dis = (folder / "all-disassembly.txt").read_text()

    def opcodes(address, symbol):
        body = dis.split(f"\n{address} <{symbol}>:\n", 1)[1].split("\n\n", 1)[0]
        return re.findall(r"^[a-f0-9]+:\s+([a-f0-9]+)\s+", body, flags=re.M)

    helper_ops = opcodes(helper["target"][2:], helper["name"])
    caller_ops = opcodes(layout["address"], layout_name)
    assert helper_ops[-1] == "8282" and caller_ops[0] == "7159"
    saves = helper_ops[:-1] + caller_ops[3 : 3 + (12 - len(helper_ops))]
    return dict(
        caller=layout, helper=helper, saves=saves, helper_opcodes=helper_ops, caller_start=caller_ops[:14]
    )


# Unit 8 rebindings are conditional on the actual emitted linkage.
before = json.loads((DATA / "bookmark-prologue.json").read_text())
after = outline(work, calls)
assert len(before["saves"]) == 11 and before["saves"] == after["saves"], (before["saves"], after["saves"])
assert spec["routes"][outline_route]["segments"][-2:] == [layout_name, "OUTLINED_FUNCTION_21"]
spec["routes"][outline_route]["segments"][-1] = after["helper"]["name"]
proof["bookmark"] = dict(reference=before, checked=after)

canonical_attestations = Path(cfg.data["linked"]["attestations_source"]["default"])
attestations = rebind(spec, calls, canonical_attestations, work)
bind_file_get(spec, by_name, work, attestations)
att = json.loads(attestations.read_text())
proof["teardown"] = {}
dtor = "core::ptr::drop_glue::<xp_ffi::app::epub::book_owner::BookCache>"
route = "session-teardown-cache-close"
steps = spec["routes"][route]["segments"]
if dtor not in by_name:
    i = steps.index(dtor)
    caller, target = steps[i - 1], steps[i + 1]
    import json

    block = (work / "all-disassembly.txt").read_text().split(f"<{caller}>:\n", 1)[1].split("\n\n", 1)[0]
    match = re.search(r"([0-9a-f]+):.*lw\s+a1,164\(a1\)\n([0-9a-f]+):.*jalr\s+a1", block)
    assert match, block
    leaf = next(row for row in leaves if row["field"] == "file_close")
    assert leaf["function"] == target and leaf["index"] * 4 == 164
    dealloc = call_edge(caller, "<alloc::raw_vec::RawVecInner>::deallocate")
    proof["teardown"]["session-teardown-cache-close"] = dict(
        caller=caller,
        caller_frame=by_name[caller]["frame"],
        callback_pc=match[2],
        only_target=leaf,
        deallocation=dealloc,
    )
    del steps[i]
    att["edges"].append(
        dict(
            caller=caller,
            callee=target,
            kind="linked callback table and field offset",
            source="RustEpubResources.cpp XpEpubLeaves file_close",
            mechanism=f"jalr at {match[2]} through sole shipping XpEpubLeaves field file_close at 164",
        )
    )
old_build = "core::ptr::drop_glue::<xp_ffi::app::epub::section_owner::driver::BuildOwner>"
new_build = "<xp_ffi::app::epub::section_owner::driver::BuildOwner as core::ops::drop::Drop>::drop"
old_stream = "<xp_ffi::app::epub::book_owner::Protection>::stream"
new_stream = "xp_ffi::app::epub::book_owner::protection::stream"
proof["build_drop"] = {}
proof["protection_stream"] = {}
for name, row in spec["routes"].items():
    steps = row["segments"]
    if old_build in steps and old_build not in by_name:
        i = steps.index(old_build)
        proof["build_drop"][name] = [call_edge(steps[i - 1], new_build), call_edge(new_build, steps[i + 1])]
        steps[i] = new_build
    if old_stream in steps and old_stream not in by_name:
        i = steps.index(old_stream)
        leaf = next(l for l in leaves if l["field"] == "protection_stream")
        assert leaf["index"] == 10 and leaf["function"] == steps[i + 1]
        fun = by_name[new_stream]
        body = (
            (work / "all-disassembly.txt")
            .read_text()
            .split("\n" + fun["address"] + " <", 1)[1]
            .split("\n\n", 1)[0]
        )
        assert re.search(r"lw\s+s0,40\(s1\)", body) and len(re.findall(r"jalr\s+s0\n", body)) == 2
        proof["protection_stream"][name] = dict(
            incoming=call_edge(steps[i - 1], new_stream), body=body, target=leaf
        )
        att["edges"].append(
            dict(
                caller=new_stream,
                callee=leaf["function"],
                kind="linked callback table and field offset",
                source="rustEpubResources protection_stream, XpEpubLeaves slot 10",
                mechanism="Both jalr s0 instructions dispatch through the single table entry at byte offset 40.",
            )
        )
        steps[i] = new_stream
proof["clippings"] = {}
proof["web_routes"] = {}
registration = "CrossPointWebServer::registerRoutes()"
dispatch = "std::function<void ()>::operator()() const"
uris = {"handleMove": "/move", "handleRename": "/rename", "handleDelete": "/delete"}
web_proofs = {}
bound_http = set()


def registered_call(caller, native, callee):
    if native in web_proofs:
        return web_proofs[native]
    uri = uris[callee.split("::")[-1].split("(")[0]]
    lines = blocks[registration].splitlines()
    (i,) = [i for i, line in enumerate(lines) if f"<{native}>" in line]
    window = lines[i - 9 : i + 8]
    assert any("sw\ta5,100(s0)" in line for line in window)
    assert any("li\ta2,3" in line for line in window)
    (j,) = [
        j
        for j in range(i + 1, i + 13)
        if "<WebServer::on(Uri const&, http_method, std::function<void ()>)>" in lines[j]
    ]
    (address,) = [
        re.search(r"# ([0-9a-f]+) <_flash_rodata_start", line)[1]
        for line in lines[i - 9 : i]
        if " <_flash_rodata_start" in line
    ]
    addr = int(address, 16)
    raw = subprocess.check_output(
        [
            tool,
            "-s",
            f"--start-address={addr:#x}",
            f"--stop-address={addr + len(uri) + 1:#x}",
            str(candidate),
        ],
        text=True,
    )
    data = b"".join(
        bytes.fromhex("".join(line.split()[1 : 1 + (len(uri) + 3) // 4]))
        for line in raw.splitlines()
        if line.strip().startswith(address)
    )
    assert data.startswith(uri.encode() + b"\0"), (data, uri, raw)
    assert "addi\ta0,s1,24" in blocks[caller] and f"<{dispatch}>" in blocks[caller]
    assert "lw\ta5,12(a0)" in blocks[dispatch] and "jr\ta5" in blocks[dispatch]
    value = dict(
        uri=uri,
        uri_data=raw,
        registration_frame=by_name[registration]["frame"],
        registration=lines[i - 9 : j + 1],
        dispatch_frame=by_name[caller]["frame"],
        dispatch=blocks[caller],
        nonempty_function_tail=blocks[dispatch],
        branch_frame=0,
        target_set=[native],
    )
    web_proofs[native] = value
    return value


dis = (work / "all-disassembly.txt").read_text()
blocks = {m[2]: m[0] for m in re.finditer(r"^([0-9a-f]+) <(.+)>:\n(?:[^\n]+\n)+", dis, re.M)}
for name, row in spec["routes"].items():
    steps = row["segments"]
    for i, (caller, callee) in enumerate(zip(steps, steps[1:])):
        if (
            callee.endswith((">>::save", ">>::unload"))
            and caller
            in (
                "xp_clippings_p18_finish_uploads",
                "xp_clippings_p18_prepare_sync",
                "xp_clippings_p18_unload",
            )
            and not any(e["name"] == callee for e in by_name[caller]["calls"])
        ):
            (via,) = {
                c["name"]
                for c in by_name[caller]["calls"]
                if ">::update::" in c["name"]
                and any(e["name"] == callee for e in by_name[c["name"]]["calls"])
            }
            proof["clippings"][name] = [call_edge(caller, via), call_edge(via, callee)]
            steps.insert(i + 1, via)
            break
        if "registerRoutes()::{lambda()" in caller and callee.startswith("CrossPointWebServer::handle"):
            target = " <" + callee + ">"
            if target in blocks[caller]:
                break
            (native,) = [
                n
                for n, b in blocks.items()
                if "registerRoutes()::{lambda()" in n and "::_M_invoke" in n and target in b
            ]
            proof["web_routes"][name] = dict(
                old=caller,
                new=native,
                instruction=blocks[native],
                prior=steps[i - 1],
                frame=by_name[native]["frame"],
                callee=by_name[callee]["frame"],
                registration=registered_call(steps[i - 1], native, callee),
            )
            if native not in bound_http:
                prior = steps[i - 1]
                att["edges"] = [
                    row for row in att["edges"] if (row["caller"], row["callee"]) != (prior, native)
                ]
                mechanism = (
                    "POST "
                    + web_proofs[native]["uri"]
                    + " selects its own FunctionRequestHandler. Its nonempty std::function starts at "
                    "object offset 24 and tail-dispatches its sole invoker at offset 12 before the original handler."
                )
                att["edges"].append(
                    dict(
                        caller=prior,
                        callee=native,
                        kind="source-selected WebServer route",
                        source="CrossPointWebServer::registerRoutes handler registration",
                        mechanism=mechanism,
                    )
                )
                bound_http.add(native)
            steps[i] = native
            break


app = "<xp_ffi::app::App>"
helper = app + "::open_control_center"
proof["control_center"] = {}


def instruction_edge(caller, callee):
    body = blocks[caller]
    instructions = [line.strip() for line in body.splitlines() if f"<{callee}>" in line]
    assert instructions, (caller, callee)
    return dict(
        caller=caller,
        caller_frame=by_name[caller]["frame"],
        callee=callee,
        callee_frame=by_name[callee]["frame"],
        instructions=instructions,
    )


for label in (
    "d1-input-frontlight-create",
    "d1-input-frontlight-queue",
    "r3-create-text-allocate",
    "r3-create-owner-allocate",
    "s1-owner-create--o5a-return-reload-read-25",
    "s1-owner-create--web-delete-xp_clippings_p17_p18_delete_for_path-tail-1",
):
    chain = spec["routes"][label]["segments"]
    caller = app + ("::definition_input" if label.startswith("d1-") else "::activity_loop")
    i = chain.index(caller)
    proof["control_center"][label] = [
        instruction_edge(caller, helper),
        instruction_edge(helper, chain[i + 1]),
    ]
    chain.insert(i + 1, helper)
retired = spec["routes"].pop("d1-input-frontlight_present")
assert "frontlight_present" not in (cfg.build_path / "rust/crates/xp-ffi/src/app/definition.rs").read_text()
proof["control_center"]["d1-input-frontlight_present"] = dict(
    retired=retired, body=blocks[app + "::definition_input"]
)
attestations.write_text(json.dumps(att, indent=2) + "\n")
cfg.data["linked"]["attestations_source"]["default"] = str(attestations)

assert len(proof["font"]) in (0, 20) and len(proof["http"]) == 20
print(
    side,
    "font:",
    len(proof["font"]),
    "HTTP helper:",
    sum(len(edges) == 2 for edges in proof["http"].values()),
    "bookmark:",
    before["helper"]["name"],
    "->",
    after["helper"]["name"],
    flush=True,
)
routes_file = work / "routes-rebound.json"
routes_file.write_text(json.dumps(spec, indent=2) + ("\n" if source.read_bytes().endswith(b"\n") else ""))
(work / "rebind-proof.json").write_text(json.dumps(proof, indent=2) + "\n")
cfg.data["linked"]["routes_source"]["default"] = str(routes_file)
for original, checked, patch in (
    (source, routes_file, "x3-default.patch"),
    (canonical_attestations, work / "attestations-dictionary.json", "x3-default-attestations.patch"),
):
    result = subprocess.check_output(["patch", "-s", "-o", "-", str(original), str(HERE / patch)])
    assert json.loads(result) == json.loads(checked.read_text()), patch
routes, edges = linked.routes(cfg, work, inspection, leaves, calls)
print(side, len(routes), "routes,", len(edges), "edges,", len(leaves), "leaf slots")


def run(script, *arguments):
    subprocess.run([sys.executable, HERE / script, *arguments], check=True)


run("bindings.py", out, options.tool_prefix)
for mode in ("sleep", "metadata"):
    run(
        "physical.py",
        "--image",
        work,
        "--fatal",
        DATA / f"{mode}-bounds.json",
        "--out",
        out / mode,
        *(["--metadata"] if mode == "metadata" else []),
    )
run("branches.py", work, out / "sleep/RESULT.json", out / "sleep-continuations.json")
run("metadata.py", out)
run("report.py", out)
