"""Named Rust IDs follow each generated table, including --strip-unused."""
from pathlib import Path
import re
import json
import sys
import subprocess
import tempfile
import unittest
import os
import shutil

ROOT = Path(__file__).resolve().parents[2]


sys.path.insert(0, str(ROOT / "scripts"))
from gen_i18n import load_translations, find_used_string_keys


class GeneratedIds(unittest.TestCase):
    def test_ids_and_strings(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            used = work / "used"
            used.mkdir()
            (used / "used.cpp").write_text("StrId::STR_FONT;\n")
            (used / "labels.rs").write_text("use xp_core::i18n::ids::STR_LAYOUT;\n")
            (used / "i18n_generated.rs").write_text("pub const STR_SELECT: u16 = 2;\n")
            (used / "I18nKeys.h").write_text("enum class StrId { STR_SELECT };\n")
            for name in ("target", "build", ".pio", ".git"):
                (used / name).mkdir()
                (used / name / "stale.rs").write_text("pub const STR_SELECT: u16 = 2;\n")
            self.assertEqual(find_used_string_keys([str(used)]), {"STR_FONT", "STR_LAYOUT"})
            translations = load_translations(str(ROOT / "lib/I18n/translations"))[4]
            for stripped in (False, True):
                output = work / str(stripped)
                output.mkdir()
                command = ["python3", str(ROOT / "scripts/gen_i18n.py"),
                           str(ROOT / "lib/I18n/translations"), str(output),
                           "--builtin-langs", "EN,FR", "--src-dirs", str(used)]
                if stripped:
                    command.append("--strip-unused")
                subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
                cpp = (output / "I18nKeys.h").read_text().split("enum class StrId", 1)[1].split("};", 1)[0]
                keys = re.findall(r"\bSTR_\w+\b", cpp)
                self.assertIn("STR_FONT", keys)
                self.assertIn("STR_LAYOUT", keys)
                self.assertEqual("STR_SELECT" in keys, not stripped)
                rust = (output / "i18n_generated.rs").read_text()
                ids = dict((key, int(value)) for key, value in re.findall(r"pub const (STR_\w+): u16 = (\d+);", rust))
                self.assertEqual(ids, {key: i for i, key in enumerate(keys)})
                # Compile and index the actual generated blob for each mode.
                program = '''use std::ffi::{c_char,CStr};
#[repr(transparent)] pub struct CStrPtr(*const c_char);
unsafe impl Sync for CStrPtr {}
include!("i18n_generated.rs");
fn main() {
 let (data,offsets)=language_strings(0);
 for (id,expected) in [(ids::STR_FONT,"Font"),(ids::STR_LAYOUT,"Layout"),(ids::STR_SELECT,"Select")] {
  let start=offsets[usize::from(id)] as usize;
  let text=unsafe {CStr::from_ptr(data.as_ptr().add(start).cast())};
  assert_eq!(text.to_str().unwrap(),expected);
 }
}
'''
                expected = ",".join(f"(ids::{key},{json.dumps(translations[key][0], ensure_ascii=False)})" for key in keys)
                program = program.replace('[(ids::STR_FONT,"Font"),(ids::STR_LAYOUT,"Layout"),(ids::STR_SELECT,"Select")]', "[" + expected + "]")
                (output / "main.rs").write_text(program)
                subprocess.run(["rustc", "--edition=2021", str(output / "main.rs"), "-o", str(output / "check")], check=True)
                subprocess.run([str(output / "check")], check=True)

    def test_cargo_rebuilds_after_rust_usage_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            core = work / "rust/crates/xp-core"
            (core / "src").mkdir(parents=True)
            (work / "src").mkdir()
            (work / "scripts").mkdir()
            shutil.copytree(ROOT / "lib/I18n/translations", work / "lib/I18n/translations")
            shutil.copy2(ROOT / "scripts/gen_i18n.py", work / "scripts/gen_i18n.py")
            shutil.copy2(ROOT / "rust/crates/xp-core/build.rs", core / "build.rs")
            (core / "Cargo.toml").write_text('[package]\nname="i18n-discovery-test"\nversion="0.1.0"\nedition="2021"\n')
            (core / "src/lib.rs").write_text('''
use std::ffi::c_char;
#[repr(transparent)] pub struct CStrPtr(*const c_char);
unsafe impl Sync for CStrPtr {}
pub mod i18n {
 use super::CStrPtr;
 include!(concat!(env!("OUT_DIR"), "/i18n_generated.rs"));
}
mod labels;
''')
            (work / "src/used.cpp").write_text("StrId::STR_FONT;\n")
            labels = core / "src/labels.rs"
            environment = dict(os.environ, XP_I18N_BUILTIN_LANGS="EN,FR", XP_I18N_STRIP_UNUSED="1",
                               CARGO_TARGET_DIR=str(work / "target"))
            for key in ("STR_LAYOUT", "STR_SELECT"):
                # Only this Rust import changes between the two Cargo builds.
                labels.write_text(f"use crate::i18n::ids::{key};\n")
                subprocess.run(["cargo", "build", "--offline", "--manifest-path", str(core / "Cargo.toml")],
                               env=environment, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                generated = next((work / "target/debug/build").glob("*/out/i18n_generated.rs"))
                ids = set(re.findall(r"pub const (STR_\w+): u16", generated.read_text()))
                self.assertEqual(ids, {"STR_FONT", key})
                output = work / "platformio"
                output.mkdir(exist_ok=True)
                # PlatformIO calls the generator defaults; Cargo supplies explicit roots.
                subprocess.run([sys.executable, str(work / "scripts/gen_i18n.py"),
                                str(work / "lib/I18n/translations"), str(output), "--strip-unused",
                                "--builtin-langs", "EN,FR"], cwd=work, check=True, stdout=subprocess.DEVNULL)
                self.assertEqual(generated.read_bytes(), (output / "i18n_generated.rs").read_bytes())


if __name__ == "__main__":
    unittest.main()
