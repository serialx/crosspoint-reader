"""Frozen native inputs are data; generate comparison translation units explicitly."""
from pathlib import Path
import hashlib
import json
import sys

HERE = Path(__file__).resolve().parent
ORACLE = HERE / "native_oracle"

def check():
    for name, digest in json.loads((ORACLE / "manifest.json").read_text()).items():
        assert hashlib.sha256((ORACLE / name).read_bytes()).hexdigest() == digest, name

if __name__ == "__main__":
    check()
    if sys.argv[1:] != ["--check"]:
        template = Path(sys.argv[1]).read_text()
        assert template.count("// NATIVE_MODE_ORACLE") == 1
        template = template.replace('// NATIVE_MODE_ORACLE', (ORACLE / 'modes_original.txt').read_text())
        template = template.replace('../ui_settings/Recorder.h', str(HERE.parent / 'ui_settings/Recorder.h'))
        Path(sys.argv[2]).write_text(template)
