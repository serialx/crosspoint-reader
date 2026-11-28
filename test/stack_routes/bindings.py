"""Record every route whose linked call chain changed since the reviewed unit."""

import json
import re
import subprocess
import sys
from pathlib import Path

out = Path(sys.argv[1])
root = out / "stack/candidate/default"
new = json.loads((root / "selected-production-chains.json").read_text())["routes"]
edge_audit = json.loads((root / "selected-edge-audit.json").read_text())["edges"]
edges = {(e["caller"], e["callee"]): e for e in edge_audit}
funcs = {f["name"]: f for line in (root / "resolved-calls.jsonl").open() if (f := json.loads(line))}
dis = (root / "all-disassembly.txt").read_text()
objdump = sys.argv[2] + "objdump"


def body(name):
    addr = funcs[name]["address"]
    return dis.split("\n" + addr + " <" + name + ">:\n", 1)[1].split("\n\n", 1)[0].splitlines()


def vrproof(name):
    target = funcs[name]["address"]
    sym = next(
        line
        for line in (root / "symbols.txt").read_text().splitlines()
        if line.endswith(" V vtable for HalFile")
    )
    vtable = int(sym.split()[0], 16)
    slot = vtable + 8 + 12
    buf = subprocess.check_output(
        [
            objdump,
            "-s",
            "--start-address=" + hex(slot),
            "--stop-address=" + hex(slot + 4),
            root / "firmware.elf",
        ],
        text=True,
    )
    word = re.search(r"(?m)^\s*[0-9a-f]+\s+([0-9a-f]{8})\s", buf)[1]
    assert int.from_bytes(bytes.fromhex(word), "little") == int(target, 16)
    return f"Concrete HalFile object from Storage.open. Vtable {vtable:#x} plus the 8-byte ABI prefix and 12-byte slot contains {target}."


def data_for(a, b):
    record = edges[a, b]
    arow, brow = funcs[a], funcs[b]
    proof = {
        "caller": a,
        "frame": arow["frame"],
        "target_set": [{"name": b, "address": brow["address"], "frame": brow["frame"]}],
    }
    linked = [*record["calls"], *record["tails"]]
    if linked:
        proof.update(
            kind="direct", sites=[c["pc"] if isinstance(c, dict) else c.split(":")[0] for c in linked]
        )
        return proof
    if a.startswith("FunctionRequestHandler::handle(") and "registerRoutes()" in b:
        web = json.loads((root / "rebind-proof.json").read_text())["web_routes"]
        binding = next(row["registration"] for row in web.values() if row["new"] == b)
        return dict(
            kind="indirect", caller=a, frame=arow["frame"], target_set=proof["target_set"], dispatch=binding
        )
    lines = body(a)

    def pc(i):
        return lines[i].split(":")[0]

    sites = []
    why = ""
    for i, line in enumerate(lines):
        if not re.search(r"\b(?:jalr|jr)\s+\w+$", line):
            continue
        three = "\n".join(lines[max(0, i - 3) : i])
        if a.endswith("xp_dictionary_lookup::combine"):
            if "lw\ta4,12(sp)" in three:
                sites.append(pc(i))
            why = "Lookup.read offset 52; the caller loads this slot into the 12(sp) spill. The XpDictionaryLookup initializer in Dictionary::lookup supplies only the readDefinition thunk."
            assert any("lw\ta0,52(a1)" in l for l in lines)
        elif a == "xp_dictionary_definition":
            slot, reg = (36, "a2") if "lambda(void*, unsigned char)" in b else (40, "a3")
            if f"lw\t{reg},{slot}(s0)" in three and line.endswith("jalr\t" + reg):
                sites.append(pc(i))
            why = f"XpDictionaryDefinition immutable callback at offset {slot}, initialized by Dictionary::readDefinition."
        elif a.startswith("ChapterPageAssembler::completeBytes("):
            if any("lw\ta5,132(s1)" in l for l in lines[max(0, i - 12) : i]):
                sites.append(pc(i))
            why = "The page-sink branch invokes completePageFn. The dictionary parser constructor in DictHtmlPages.cpp binds the sole page-completion lambda for this owner. The adjacent byte-sink branch is a different std::function."
        elif a == "xp_dictionary_html" or a.endswith("html::Writer>::append"):
            sites.append(pc(i))
            why = "writeNormalizedXhtml in src/util/DictHtmlPages.cpp supplies its sole HalFile output callback to xp_dictionary_html; the Writer retains the same callback."
        elif "writeNormalizedXhtml" in a and b.startswith("HalFile::write("):
            if line.endswith("jr\ta5") and "lw\ta5,12(a5)" in three:
                sites.append(pc(i))
            why = vrproof(b)
        elif a.endswith("DefinitionSession>::input_header"):
            assert any("mv\ts0,a0" in l for l in lines[:14])
            if line.endswith("jalr\ts0"):
                sites.append(pc(i))
            source = a.removesuffix("_header")
            window = body(source)
            for j, instruction in enumerate(window):
                if "<" + a + ">" in instruction:
                    slot_load = window[j - 8 : j + 1]
                    assert any("lw\ta0,48(a0)" in l for l in slot_load), slot_load
                    result = {"caller": source, "frame": funcs[source]["frame"], "instructions": slot_load}
                    break
            else:
                raise ValueError((source, a))
            why = "DefinitionSession::input loads DefinitionPhysical.layout at offset 48 and passes it as a0. The helper retains it in s0 and calls it (definition_input.rs:13–16); RustEpubResources.cpp:1116 supplies the layout callback."
        elif a.startswith("core::ptr::drop_glue::<") and "DefinitionOwner" in a:
            if line.endswith("jalr\ta1") and "lw\ta1,12(a0)" in three:
                sites.append(pc(i))
            why = "DefinitionOwner::drop (definition_owned.rs:207–216) reads DefinitionPhysical.payload_drop; the sole child_definition_payload_drop initializer is RustEpubResources.cpp:1077."
        elif a == "xp_dictionary_ifo":
            if line.endswith("jalr\ta3") and "lw\ta3,4(a1)" in three:
                sites.append(pc(i))
            why = "The bounded IFO scan calls XpDictionaryFile.read at offset 4; Dictionary.cpp:43–48 supplies only dictionaryFile(ifo), whose read thunk is DictionaryIo.h:5–7."
        elif a == "(anonymous namespace)::progress(void*, unsigned int, unsigned int)" and "_M_invoke" in b:
            if line.endswith("jalr\ta5") and any("lw\ta5,12(a0)" in l for l in lines[:i]):
                sites.append(pc(i))
            (caller,) = [n for n in funcs if n.startswith("CatalogActivity::downloadFile(")]
            producer = body(caller)
            found = [
                producer[j - 2 : j + 6]
                for j, instruction in enumerate(producer)
                if "<" + b + ">" in instruction
            ]
            assert len(found) == 1, (caller, b)
            result = {"caller": caller, "frame": funcs[caller]["frame"], "instructions": found[0]}
            why = "The null POST-body file GET supplies the second CatalogActivity progress lambda to the synchronous HttpDownloader callback. Its RV32 std::function invoker at offset 12 is the sole target for these GET recipes."
        elif a in {
            "xp_ffi::app::epub::book_owner::protection::stream",
            "<xp_ffi::app::epub::book_owner::Protection>::stream",
        } and b.startswith("freeink::content::readerProtectionStream("):
            leaf = next(
                r
                for r in json.loads((root / "linked-epub-leaves.json").read_text())["leaves"]
                if r["field"] == "protection_stream"
            )
            assert leaf["index"] == 10 and leaf["function"] == b
            if line.endswith("jalr\ts7") and any("lw\ts7,40(s0)" in l for l in lines[:i]):
                sites.append(pc(i))
            assert "<" + b + ">" in dis
            result = {
                "table": "rustEpubResources()::leaves",
                "offset": 40,
                "file": "linked-epub-leaves.json",
            }
            why = "The single production XpEpubLeaves::protection_stream slot at offset 40 resolves to the pinned SDK readerProtectionStream. The helper loads it once for the synchronous Prepare and Read calls."
        else:
            raise ValueError((a, b, record))
    assert sites, (a, b)
    proof.update(kind="indirect", sites=sites, binding=why)
    if a.endswith("DefinitionSession>::input_header"):
        proof["physical_layout_argument"] = result
    if a == "(anonymous namespace)::progress(void*, unsigned int, unsigned int)":
        proof["get_callback_initializer"] = result
    if a in {
        "xp_ffi::app::epub::book_owner::protection::stream",
        "<xp_ffi::app::epub::book_owner::Protection>::stream",
    }:
        proof["linked_leaf_slot"] = result
    return proof


bindings = json.loads((Path(__file__).parent / "data/sync-bindings.json").read_text())
proof = []
for binding in bindings:
    caller, target = binding["caller"], binding["callee"]
    proof.append(data_for(caller, target))
(out / "sync-binding-proof.json").write_text(json.dumps(proof, indent=2) + "\n")
print(len(proof), "sync bindings verified")
