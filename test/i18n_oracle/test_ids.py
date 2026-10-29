"""Named Rust IDs follow each generated table, including --strip-unused."""
from pathlib import Path
import re
import json
import sys
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


sys.path.insert(0, str(ROOT / "scripts"))
from gen_i18n import load_translations


class GeneratedIds(unittest.TestCase):
    def test_ids_and_strings(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            used = work / "used"
            used.mkdir()
            (used / "used.cpp").write_text("StrId::STR_FONT; StrId::STR_LAYOUT; StrId::STR_SELECT;\n")
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


if __name__ == "__main__":
    unittest.main()
