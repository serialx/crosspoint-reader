"""Compare Rust RSSI to frozen C++ and check process-lifetime clock wrap."""
from pathlib import Path
import shutil
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    shutil.copyfile(HERE/'web_policy.rs',out/'main.rs')
    shutil.copyfile(ROOT/'rust/crates/xp-ui/src/network/web.rs',out/'web.rs')
    subprocess.run(['rustc','--edition=2021',str(out/'main.rs'),'-o',str(out/'policy')],check=True)
    actual = subprocess.check_output([str(out/'policy')]).decode().splitlines()
    expected = [line for line in (HERE/'effects.golden').read_text().splitlines() if line.startswith('rssi:')]
    assert len(expected) == 707 and actual == expected
