#!/usr/bin/env python3
"""Exercise the port gate using recorded GNU text, independent of host object format."""

import copy
import importlib.util
import runpy
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "ports", ROOT / "scripts/check_rust_ports.py"
)
ports = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ports)
FIXTURE = ROOT / "test/fixtures/rust_ports"


class HookEnvironment:
    def __init__(self, mode="enforce", profile="default"):
        self.mode, self.profile = mode, profile
        self.dependencies, self.actions = [], []

    def subst(self, name):
        return {
            "$PIOENV": self.profile,
            "$PROJECT_DIR": str(ROOT),
            "$PYTHONEXE": "python3",
            "$BUILD_DIR": "build",
        }[name]

    def GetProjectOption(self, name, default):
        return self.mode

    def PioPlatform(self):
        return self

    def get_package_dir(self, package):
        return "/tools"

    def Value(self, value):
        return ("SCons value", value)

    def Depends(self, target, dependencies):
        self.dependencies.extend(dependencies)

    def VerboseAction(self, action, description):
        return action

    def AddPreAction(self, target, action):
        self.actions.append(action)


class PortTests(unittest.TestCase):
    def setUp(self):
        self.native = (FIXTURE / "native.readelf").read_text()
        self.rust = ports.rust_symbols((FIXTURE / "rust.nm").read_text())
        self.config = ports.policy(FIXTURE / "policy.txt")
        self.headers = {"ports.h": (FIXTURE / "ports.h").read_text()}
        self.lock = {
            tuple(line.split())
            for line in (FIXTURE / "ports.lock").read_text().splitlines()
        }

    def run_check(self):
        paths, _, errors = ports.analyse(
            ports.objects(self.native.splitlines()),
            self.rust,
            self.config,
            self.headers,
        )
        differences, summary = ports.compare(paths, self.lock, self.config)
        return errors + differences, summary

    def test_physical_allowed(self):
        errors, summary = self.run_check()
        self.assertEqual(errors, [])
        self.assertIn("'physical': 1", summary)
        self.assertIn("common path clean: no", summary)

    def test_new_pair_fails(self):
        self.lock.remove(("Reader.bidi", "xp_bidi_reorder"))
        self.assertTrue(any("new Reader.bidi" in e for e in self.run_check()[0]))

    def test_stale_lock_fails(self):
        self.lock.add(("Reader.bidi", "removed_export"))
        self.assertTrue(
            any("stale Reader.bidi -> removed_export" in e for e in self.run_check()[0])
        )

    def test_table_size_mismatch_fails(self):
        self.native = self.native.replace("8 OBJECT", "12 OBJECT")
        with self.assertRaisesRegex(ValueError, "table size/slot/relocation mismatch"):
            self.run_check()

    def test_missing_and_duplicate_relocations_fail(self):
        original = self.native
        for replacement in ["", "00000000 00000301 R_RISCV_32 00000000 panel + 0"]:
            self.native = original.replace(
                "00000004 00000301 R_RISCV_32 00000000 panel + 0", replacement
            )
            with self.assertRaisesRegex(
                ValueError, "table size/slot/relocation mismatch"
            ):
                self.run_check()

    def test_r1_bidi_workspace_regression(self):
        self.native = self.native.replace(
            "00000000 00000313 R_RISCV_CALL_PLT 00000000 xp_bidi_reorder + 0",
            "00000000 00000313 R_RISCV_CALL_PLT 00000000 xp_bidi_reorder + 0\n00000004 00000413 R_RISCV_CALL_PLT 00000000 xp_bidi_workspace_size + 0\n00000008 00000513 R_RISCV_CALL_PLT 00000000 xp_bidi_workspace_align + 0",
        )
        self.native = self.native.replace(
            "File: ports.cpp.o",
            "  4: 00000000 0 NOTYPE GLOBAL DEFAULT UND xp_bidi_workspace_size\n"
            "  5: 00000000 0 NOTYPE GLOBAL DEFAULT UND xp_bidi_workspace_align\n"
            "File: ports.cpp.o",
        )
        errors, _ = self.run_check()
        for name in ["xp_bidi_workspace_size", "xp_bidi_workspace_align"]:
            self.assertTrue(
                any(
                    "new Reader.bidi -> " + name in e and ".text.bidi_wrapper" in e
                    for e in errors
                )
            )

    def test_physical_reuse_by_new_caller_fails(self):
        self.native = self.native.replace(
            "00000000 00000201 R_RISCV_32 00000000 xp_keyboard_metrics + 0",
            "00000000 00000301 R_RISCV_32 00000000 panel + 0",
        )
        self.assertTrue(
            any("new Reader.bidi -> xp_panel" in e for e in self.run_check()[0])
        )

    def test_layout_relocation_is_irrelevant(self):
        self.native = self.native.replace("000034", "000084").replace(
            "offset 0x100", "offset 0x900"
        )
        self.assertEqual(self.run_check()[0], [])

    def test_deferred_owners(self):
        for owner in ["R5", "R6", "R7"]:
            config = copy.deepcopy(self.config)
            config["deferred"] = {r: [owner] for r in config["inherited"]}
            config["inherited"] = {}
            self.config = config
            errors, summary = self.run_check()
            self.assertEqual(errors, [])
            self.assertIn("common path clean: yes", summary)
            # Ownership is not permission to grow the pair lock.
            self.lock.remove(("Reader.bidi", "xp_bidi_reorder"))
            self.assertTrue(any("new Reader.bidi" in e for e in self.run_check()[0]))
            self.setUp()

    def test_unknown_owner_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "policy.txt"
            path.write_text("deferred Reader.bidi R99 # Unknown task.\n")
            with self.assertRaisesRegex(ValueError, "unknown owner"):
                ports.policy(path)

    def test_hex_symbol_size(self):
        self.native = self.native.replace("8 OBJECT", "0x8 OBJECT")
        self.assertEqual(self.run_check()[0], [])

    def test_missing_symbol_metadata_fails(self):
        self.native = self.native.replace(
            "00000313 R_RISCV_CALL_PLT", "00009913 R_RISCV_CALL_PLT"
        )
        with self.assertRaisesRegex(ValueError, "missing relocation symbol"):
            self.run_check()

    def test_slot_uses_symbol_index_not_duplicate_local_name(self):
        self.native = self.native.replace(
            "Relocation section '.rela.rodata.PORTS'",
            "  [ 3] .text.unrelated PROGBITS 00000000 000054 000010 00 AX 0 0 4\n"
            "Relocation section '.rela.rodata.PORTS'",
        ).replace(
            "File: KeyboardEntryActivity.cpp.o",
            "  5: 00000000 16 FUNC LOCAL DEFAULT 3 panel\n"
            "File: KeyboardEntryActivity.cpp.o",
        )
        self.assertEqual(self.run_check()[0], [])

    def test_rust_definition_is_not_hidden_by_a_native_weak_body(self):
        exports, imports = self.rust
        self.rust = (exports | {"bidi_wrapper"}, imports - {"bidi_wrapper"})
        self.native = self.native.replace(
            "16 FUNC GLOBAL DEFAULT 2 bidi_wrapper",
            "16 FUNC WEAK DEFAULT 2 bidi_wrapper",
        )
        self.native = self.native.replace(
            "Relocation section '.rela.text.bidi_wrapper'",
            "Relocation section '.rela.text.xp_native_bidi_acquire' at offset 0x100 contains 1 entry:\n"
            "00000000 00000213 R_RISCV_CALL_PLT 00000000 bidi_wrapper + 0\n"
            "Relocation section '.rela.text.bidi_wrapper'",
        )
        errors, _ = self.run_check()
        self.assertTrue(
            any("new xp_native_bidi_acquire -> bidi_wrapper" in e for e in errors)
        )
        self.assertTrue(any("new Reader.bidi -> bidi_wrapper" in e for e in errors))

    def test_hook_arguments_failure_and_mode_dependency(self):
        for mode in ["report", "enforce"]:
            env = HookEnvironment(mode)
            runpy.run_path(
                str(ROOT / "scripts/check_rust_ports_build.py"),
                init_globals={"env": env, "Import": lambda name: None},
            )
            self.assertIn(env.Value(mode), env.dependencies)
            self.assertEqual(len(env.actions), 1)
            with mock.patch("subprocess.call", return_value=1) as call:
                self.assertEqual(env.actions[0]([], [], env), 1)
            command = call.call_args.args[0]
            self.assertTrue(all(isinstance(part, str) for part in command), command)
            self.assertEqual("--report" in command, mode == "report")

    def test_hook_skips_other_boards(self):
        env = HookEnvironment(profile="x4pro")
        runpy.run_path(
            str(ROOT / "scripts/check_rust_ports_build.py"),
            init_globals={"env": env, "Import": lambda name: None},
        )
        self.assertEqual(env.actions, [])

    def test_native_directory_named_rust_is_not_cargo(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native, cargo = root / "src/rust/port.cpp.o", root / "rust/ffi.rcgu.o"
            for path in [native, cargo]:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            self.assertEqual(ports.object_files(root), [native])

    def test_weak_fallback_requires_a_native_definition(self):
        nm = "libxp_ffi.a[compiler_builtins-abc.rcgu.o]:\nmemcpy W 0 10\nnovel W 0 10\n"
        nm += "libxp_ffi.a[ffi.rcgu.o]:\napplication W 0 10\n"
        defined, _ = ports.rust_symbols(nm, {"memcpy", "application"})
        self.assertEqual(defined, {"novel", "application"})

    def test_callback_slot_requires_policy(self):
        self.headers["ports.h"] = self.headers["ports.h"].replace(
            "(*panel)(void)", "(*panel)(void (*sink)(void))"
        )
        self.assertTrue(any("callback-taking slot" in e for e in self.run_check()[0]))
        self.config["callback"]["Reader.panel"] = []
        self.assertEqual(self.run_check()[0], [])

    def test_unknown_static_table_is_walked(self):
        self.native = self.native.replace(
            ".text.bidi_wrapper", ".rodata.unknown_callbacks"
        ).replace(
            "16 FUNC GLOBAL DEFAULT 2 bidi_wrapper",
            "16 OBJECT GLOBAL DEFAULT 2 bidi_wrapper",
        )
        self.lock.remove(("Reader.bidi", "xp_bidi_reorder"))
        self.assertTrue(
            any(
                "new Reader.bidi" in e and ".rodata.unknown_callbacks" in e
                for e in self.run_check()[0]
            )
        )


if __name__ == "__main__":
    unittest.main()
