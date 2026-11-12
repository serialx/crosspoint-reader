#!/usr/bin/env python3
"""Fast synthetic linked-graph checks; no target tools or firmware required."""

import contextlib
import copy
import importlib.util
import io
import json
import pathlib
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "reader_chain_guard", ROOT / "scripts/reader_chain_guard.py"
)
guard = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(guard)


def node(name, language, targets=()):
    return {
        "name": name,
        "language": language,
        "object": language + ".o",
        "edges": [{"target": t, "pc": "0x10", "kind": "jal"} for t in targets],
        "indirect": [],
        "references": [],
    }


class ChainGuardTests(unittest.TestCase):
    def setUp(self):
        fixture = json.loads((ROOT / "test/reader_chain_guard/graph.json").read_text())
        self.nodes = {
            int(address): value for address, value in fixture["nodes"].items()
        }
        self.roots = fixture["roots"]

    def report(self):
        return guard.analyse(self.nodes, self.roots)

    def allowlist(self):
        return {
            "physical": [],
            "inherited": [
                {
                    "id": b["id"],
                    "native": b["native"],
                    "callers": b["callers"],
                    "returns": b["returns"],
                    "owner": "R1",
                }
                for b in self.report()["boundaries"]
            ],
        }

    def test_cycles_first_return_and_repeated_crossings(self):
        report = self.report()
        bounds = {b["native"]: b for b in report["boundaries"]}
        self.assertEqual(set(bounds), {"native_front", "second_front"})
        self.assertEqual(bounds["native_front"]["returns"], ["rust_return"])
        self.assertEqual(bounds["second_front"]["callers"], ["rust_return"])
        self.assertEqual(guard.compare(report, self.allowlist()), [])

    def test_new_and_stale_return_cannot_hide_in_existing_family(self):
        allowed = self.allowlist()
        self.nodes[3]["edges"].append({"target": 5, "kind": "j", "pc": "0x20"})
        self.assertEqual(
            guard.compare(self.report(), allowed)[0]["kind"], "new-returns"
        )
        self.nodes[3]["edges"] = []
        self.assertIn(
            "stale-boundary", {e["kind"] for e in guard.compare(self.report(), allowed)}
        )

    def test_new_caller_and_stale_caller(self):
        allowed = self.allowlist()
        self.nodes[5]["edges"].append({"target": 2, "kind": "jal", "pc": "0x22"})
        self.assertEqual(
            guard.compare(self.report(), allowed)[0]["kind"], "new-callers"
        )
        self.assertEqual(
            guard.compare(
                guard.analyse(
                    {1: self.nodes[1], 2: node("native_front", "native")}, {}
                ),
                allowed,
            )[0]["kind"],
            "stale-boundary",
        )

    def test_one_stale_return_and_caller_are_rejected(self):
        self.nodes[3]["edges"].append({"target": 5, "kind": "j", "pc": "0x20"})
        self.nodes[5]["edges"].append({"target": 2, "kind": "jal", "pc": "0x22"})
        allowed = self.allowlist()
        self.nodes[3]["edges"].pop()
        self.nodes[5]["edges"].pop()
        kinds = {e["kind"] for e in guard.compare(self.report(), allowed)}
        self.assertEqual(kinds, {"stale-returns", "stale-callers"})

    def test_source_callbacks_retire_with_table_field(self):
        config = {
            "callback_bindings": [
                {
                    "roots": ["epub.test"],
                    "target": "rust_return",
                    "source": "fixture",
                    "reason": "test sink",
                }
            ]
        }
        guard.add_source_callbacks(self.nodes, {"table:epub.test": 6}, config)
        self.assertIn(4, [e["target"] for e in self.nodes[6]["edges"]])
        config["callback_bindings"][0]["target"] = "removed_callback"
        guard.add_source_callbacks(self.nodes, {}, config)
        with self.assertRaises(ValueError):
            guard.add_source_callbacks(self.nodes, {"table:epub.test": 6}, config)

    def test_table_decode_includes_anonymous_rust_data(self):
        class SyntheticElf:
            symbols = (("named", 0x1000, 8, 1),)
            sections = ((0, 1, 2, 0x1000, 0, 0x100, 0, 0, 0, 0),)

            def read(self, address, size):
                return {
                    0x1000: b"\x00\x00\x00\x00\x02\x00\x00\x00",
                    0x1010: b"\x04\x00\x00\x00",
                }[address]

        tables = guard.decode_tables(
            SyntheticElf(),
            self.nodes,
            {"named": "callbacks"},
            [(0x1010, 0x1014, "lib.a(x.rcgu.o)")],
        )
        self.assertEqual(tables[0x1000]["entries"], [{"offset": 4, "target": 2}])
        self.assertEqual(tables[0x1010]["entries"], [{"offset": 0, "target": 4}])

    def test_source_evidence_drift_fails_closed(self):
        guard.check_source_evidence({})
        with self.assertRaisesRegex(ValueError, "source callback evidence changed"):
            guard.check_source_evidence(
                {
                    "source_fingerprints": {
                        "scripts/reader_chain_guard.py": "not-the-source-hash"
                    }
                }
            )

    def test_rom_targets_are_reported_separately(self):
        source = """42000000 <reader>:
42000000: 000080e7 jalr 0(ra) # 40000358 <memcpy>
42000004: 000780e7 jalr a5
42000008: 8082 ret
"""
        nodes = guard.parse_disassembly(source, lambda _: "lib.a(x.rcgu.o)")
        report = guard.analyse(nodes, {"reader": 0x42000000})
        self.assertEqual(len(report["unresolved_external"]), 1)
        self.assertEqual(len(report["unresolved_indirect"]), 1)

    def test_indirect_sites_remain_visible_with_candidate_edges(self):
        self.nodes[2]["indirect"] = [{"pc": "0x24", "instruction": "jalr a5"}]
        self.assertEqual(
            self.report()["unresolved_indirect"][0]["function"], "native_front"
        )

    def test_no_symbol_name_language_heuristic(self):
        self.assertEqual(guard.object_language("build/libxp_ffi.a(foo.rcgu.o)"), "rust")
        self.assertEqual(guard.object_language("build/RustAdapter.cpp.o"), "native")
        self.assertEqual(guard.object_language(""), "unknown")
        self.assertFalse(
            any(
                b["native"] == "xp_native_is_actually_rust"
                for b in self.report()["boundaries"]
            )
        )

    def test_allowlist_metadata_and_duplicates(self):
        allowed = self.allowlist()
        allowed["inherited"][0]["owner"] = "R8"
        with self.assertRaises(ValueError):
            guard.compare(self.report(), allowed)
        allowed = self.allowlist()
        allowed["physical"] = [copy.deepcopy(allowed["inherited"][0])]
        with self.assertRaises(ValueError):
            guard.compare(self.report(), allowed)

    def test_riscv_tail_call_branch_and_indirect(self):
        source = """42000000 <rust_reader>:
42000000: 000000ef jal 42000020 <native_entry>
42000004: 00000013 addi a0,a0,0 # 3c000000 <table>
42000008: 000080e7 jalr -4(ra) # 42000020 <native_entry>
4200000c: 000780e7 jalr a5
42000010: 8082 ret
42000020 <native_entry>:
42000020: a001 j 42000040 <rust_return>
42000022: c001 beqz a0,42000024 <native_entry+0x4>
42000024: 8082 ret
42000040 <rust_return>:
42000040: 8082 ret
"""
        nodes = guard.parse_disassembly(
            source, lambda a: "native.cpp.o" if a == 0x42000020 else "lib.a(x.rcgu.o)"
        )
        self.assertEqual(len(nodes[0x42000000]["indirect"]), 1)
        self.assertEqual(len(nodes[0x42000020]["edges"]), 1)
        report = guard.analyse(nodes, {"reader": 0x42000000})
        self.assertEqual(report["boundaries"][0]["returns"], ["rust_return"])

    def test_table_and_stack_callback_candidates(self):
        self.nodes[2]["references"] = [{"address": 0x1000, "pc": "0x30"}]
        tables = {
            0x1000: {
                "size": 8,
                "name": "callback table",
                "entries": [{"offset": 4, "target": 6}],
            }
        }
        guard.add_references(self.nodes, tables)
        # Native caller supplies the table to a Rust engine; its callback re-enters Rust.
        self.nodes[2]["edges"].append({"target": 5, "pc": "0x34", "kind": "jal"})
        guard.add_callback_candidates(self.nodes)
        self.assertIn(6, [e["target"] for e in self.nodes[5]["edges"]])
        self.assertIn(
            "xp_native_is_actually_rust",
            next(
                b for b in self.report()["boundaries"] if b["native"] == "second_front"
            )["callers"],
        )

    def test_map_language_uses_linked_ranges_not_discarded_sections(self):
        ranges, objects = guard.map_inventory("""LOAD libxp_ffi.a
 .text.dead 0x00000000 0x10 libxp_ffi.a(dead.rcgu.o)
 .text.live
                0x42000000 0x20 libxp_ffi.a(live.rcgu.o)
 .text.native 0x42000020 0x10 RustBridge.cpp.o
""")
        self.assertEqual(objects, ["libxp_ffi.a"])
        self.assertEqual(len(ranges), 2)
        self.assertEqual(guard.object_language(ranges[0][2]), "rust")


class PreventionTests(unittest.TestCase):
    setUp = ChainGuardTests.setUp
    report = ChainGuardTests.report
    allowlist = ChainGuardTests.allowlist

    def baseline(self):
        report = self.report()
        return {
            "contract_reason": "Reviewed fixture code",
            "sites": {
                site["id"]: {"reason": "Reviewed fixture callback"}
                for site in report["unresolved_indirect"]
            },
            "contracts": report["contracts"],
        }

    def physical(self):
        allowed = self.allowlist()
        entry = allowed["inherited"].pop(0)
        actual = next(b for b in self.report()["boundaries"] if b["id"] == entry["id"])
        entry.update(
            reason="Fixture SDK continuation",
            call_sites=actual["call_sites"],
            native_contract=actual["native_contract"],
        )
        allowed["physical"].append(entry)
        return allowed

    def test_new_non_xp_indirect_relay_is_rejected(self):
        allowed = self.allowlist()
        baseline = self.baseline()
        self.nodes[9] = node("relay", "native")
        self.nodes[9]["indirect"] = [{"pc": "0x90", "instruction": "jalr a5"}]
        self.nodes[1]["edges"].append({"target": 9, "pc": "0x44", "kind": "jal"})
        report = self.report()
        self.assertEqual(guard.compare(report, allowed), [])
        self.assertIn(
            "new-unresolved",
            {e["kind"] for e in guard.compare_uncertainty(report, baseline)},
        )

    def test_cli_rejects_non_xp_indirect_relay(self):
        allowed, baseline = self.allowlist(), self.baseline()
        self.nodes[9] = node("relay", "native")
        self.nodes[9]["indirect"] = [{"pc": "0x90", "instruction": "jalr a5"}]
        self.nodes[1]["edges"].append({"target": 9, "pc": "0x44", "kind": "jal"})
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "allowed.json").write_text(json.dumps(allowed))
            (path / "unknown.json").write_text(json.dumps(baseline))
            argv = [
                "guard",
                "--allowlist",
                str(path / "allowed.json"),
                "--uncertainty-baseline",
                str(path / "unknown.json"),
                "--report",
                str(path / "report.json"),
            ]
            with (
                mock.patch.object(guard, "build_graph", return_value=self.report()),
                mock.patch("sys.argv", argv),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                self.assertEqual(guard.main(), 1)
            errors = json.loads((path / "report.json").read_text())["errors"]
            self.assertIn("new-unresolved", {e["kind"] for e in errors})

    def test_unknown_baseline_requires_a_review_reason(self):
        self.nodes[2]["indirect"] = [{"pc": "0x20", "instruction": "jalr a5"}]
        baseline = self.baseline()
        next(iter(baseline["sites"].values()))["reason"] = ""
        with self.assertRaisesRegex(ValueError, "reviewed reason"):
            guard.compare_uncertainty(self.report(), baseline)

    def test_stale_and_same_count_replaced_unknown_site(self):
        self.nodes[2]["indirect"] = [{"pc": "0x20", "instruction": "jalr a5"}]
        baseline = self.baseline()
        self.nodes[2]["indirect"] = [{"pc": "0x30", "instruction": "jr a4"}]
        kinds = {e["kind"] for e in guard.compare_uncertainty(self.report(), baseline)}
        self.assertTrue({"new-unresolved", "stale-unresolved"} <= kinds)
        self.nodes[2]["indirect"] = []
        self.assertIn(
            "stale-unresolved",
            {e["kind"] for e in guard.compare_uncertainty(self.report(), baseline)},
        )

    def test_unknown_site_address_move_is_not_a_new_identity(self):
        self.nodes[2]["indirect"] = [{"pc": "0x20", "instruction": "jalr a5"}]
        baseline = self.baseline()
        self.nodes[2]["indirect"][0]["pc"] = "0x9000"
        self.assertEqual(guard.compare_uncertainty(self.report(), baseline), [])

    def test_valid_physical_and_missing_reason(self):
        allowed = self.physical()
        self.assertEqual(guard.compare(self.report(), allowed), [])
        del allowed["physical"][0]["reason"]
        with self.assertRaisesRegex(ValueError, "reason"):
            guard.compare(self.report(), allowed)

    def test_physical_reused_by_new_caller_or_same_caller_new_site(self):
        allowed = self.physical()
        self.nodes[5]["edges"].append({"target": 2, "pc": "0x88", "kind": "jal"})
        self.assertIn(
            "new-callers", {e["kind"] for e in guard.compare(self.report(), allowed)}
        )
        self.nodes[5]["edges"] = []
        self.nodes[1]["edges"].append({"target": 2, "pc": "0x99", "kind": "jal"})
        self.assertIn(
            "new-call_sites", {e["kind"] for e in guard.compare(self.report(), allowed)}
        )

    def test_longer_physical_route_with_same_endpoint_is_rejected(self):
        allowed = self.physical()
        self.nodes[9] = node("new_decision", "native", [4])
        self.nodes[2]["edges"].append({"target": 9, "pc": "0x90", "kind": "jal"})
        self.assertIn(
            "changed-native-contract",
            {e["kind"] for e in guard.compare(self.report(), allowed)},
        )

    def test_existing_unknown_reused_by_changed_supplier_is_rejected(self):
        self.nodes[2]["indirect"] = [{"pc": "0x20", "instruction": "jalr a5"}]
        baseline = self.baseline()
        self.nodes[1]["code"] = ["store different_callback into shared_context"]
        self.assertIn(
            "changed-contract",
            {e["kind"] for e in guard.compare_uncertainty(self.report(), baseline)},
        )

    def test_new_boundary_via_new_named_table_is_rejected(self):
        allowed = self.allowlist()
        source = """42000000 <reader>:
42000000: 00000013 addi a0,a0,0 # 3c000000 <brand_new_callbacks>
42000004: 000780e7 jalr a5
42000020 <brand_new_native>:
42000020: 000000ef jal 42000040 <rust_callback>
42000040 <rust_callback>:
42000040: 8082 ret
"""
        nodes = guard.parse_disassembly(
            source, lambda a: "new.cpp.o" if a == 0x42000020 else "lib.a(x.rcgu.o)"
        )

        class FixtureElf:
            symbols = (("new", 0x3C000000, 4, 1),)
            sections = ()

            def read(self, address, size):
                return bytes.fromhex("20000042")

        tables = guard.decode_tables(
            FixtureElf(), nodes, {"new": "brand_new_callbacks"}
        )
        guard.add_references(nodes, tables)
        report = guard.analyse(nodes, {"reader": 0x42000000})
        self.assertIn(
            "new-boundary", {e["kind"] for e in guard.compare(report, allowed)}
        )

    def test_undecodable_new_table_still_gates_the_unknown_call(self):
        baseline = self.baseline()
        source = """42000300 <dynamic_table_caller>:
42000300: 00052783 lw a6,0(a0) # 3fc90000 <new_bss_callbacks>
42000304: 9802 jalr a6
"""
        self.nodes.update(guard.parse_disassembly(source, lambda _: "new.cpp.o"))
        self.nodes[1]["edges"].append(
            {"target": 0x42000300, "pc": "0x30", "kind": "jal"}
        )

        class FixtureElf:
            symbols = (("bss", 0x3FC90000, 4, 1),)
            sections = ()

            def read(self, address, size):
                raise ValueError("SHT_NOBITS has no linked bytes")

        tables = guard.decode_tables(
            FixtureElf(), self.nodes, {"bss": "new_bss_callbacks"}
        )
        self.assertEqual(tables, {})
        guard.add_references(self.nodes, tables)
        self.assertIn(
            "new-unresolved",
            {e["kind"] for e in guard.compare_uncertainty(self.report(), baseline)},
        )

    def test_pointer_load_slots_and_relocation_are_stable(self):
        def parsed(base):
            source = f"""{base:08x} <reader>:
{base:08x}: 00052783 lw a5,24(a0)
{base + 4:08x}: 0007a783 lw a5,8(a5)
{base + 8:08x}: 000780e7 jalr a5
{base + 12:08x}: 8082 ret
"""
            return guard.analyse(
                guard.parse_disassembly(source, lambda _: "reader.cpp.o"),
                {"reader": base},
            )

        first, moved = parsed(0x42000000), parsed(0x42001000)
        self.assertEqual(first["contracts"], moved["contracts"])
        self.assertEqual(
            first["unresolved_indirect"][0]["id"], moved["unresolved_indirect"][0]["id"]
        )
        self.assertIn("+24]+8]", first["unresolved_indirect"][0]["pointer_source"])

    def test_indirect_call_via_ra_is_not_a_return(self):
        source = """42000000 <reader>:
42000000: 000080e7 jalr ra
42000004: 8082 jr ra
"""
        nodes = guard.parse_disassembly(source, lambda _: "reader.cpp.o")
        self.assertEqual(len(nodes[0x42000000]["indirect"]), 1)

    def test_numeric_lui_change_is_not_normalized_away(self):
        def contract(value):
            source = f"""42000000 <native>:
42000000: 00000537 lui a0,{value}
42000004: 8082 ret
"""
            nodes = guard.parse_disassembly(source, lambda _: "native.cpp.o")
            return guard.analyse(nodes, {"reader": 0x42000000})["contracts"]

        self.assertNotEqual(contract("0x10"), contract("0x20"))

    def test_global_pointer_source_uses_symbol_not_relocation_offset(self):
        def sites(offset):
            source = f"""42000000 <native>:
42000000: 000007b7 lui a5,0x3c000
42000004: 0007a783 lw a5,{offset}(a5) # 3c000100 <callbacks+0x8>
42000008: 9782 jalr a5
"""
            nodes = guard.parse_disassembly(source, lambda _: "native.cpp.o")
            return list(guard.site_identities(nodes[0x42000000]))

        self.assertEqual(sites(100)[0]["id"], sites(200)[0]["id"])
        self.assertIn("lw[symbol:callbacks+0x8]", sites(100)[0]["pointer_source"])

    def test_rust_codegen_hashes_do_not_define_contract_identity(self):
        left = (
            "build/lib.a(xp_ffi-1234567890abcdef.xp_ffi.1234567890abcdef-cgu.1.rcgu.o)"
        )
        right = left.replace("1234567890abcdef", "abcdef1234567890")
        self.assertEqual(guard.object_id(left), guard.object_id(right))

    def test_ambiguous_lto_object_and_duplicate_unknown_ids_fail_closed(self):
        self.assertEqual(guard.object_language("mixed.ltrans0.ltrans.o"), "unknown")
        self.nodes[2]["indirect"] = [{"pc": "0x20", "instruction": "jalr a5"}]
        baseline = self.baseline()
        report = self.report()
        report["unresolved_indirect"].append(report["unresolved_indirect"][0])
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            guard.compare_uncertainty(report, baseline)

    def test_r1b_owner_is_valid_and_unknown_task_is_not(self):
        allowed = self.allowlist()
        allowed["inherited"][0]["owner"] = "R1b"
        self.assertEqual(guard.compare(self.report(), allowed), [])
        allowed["inherited"][0]["owner"] = "R99"
        with self.assertRaises(ValueError):
            guard.compare(self.report(), allowed)


if __name__ == "__main__":
    unittest.main()
