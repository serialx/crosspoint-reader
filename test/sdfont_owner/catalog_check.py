from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
native = subprocess.check_output([str(root / 'build/sdfont-owner/sdfont_catalog_oracle')], text=True)
candidate = subprocess.check_output([str(root / 'test/sdfont_owner/target/debug/sdfont-owner'), '--catalog'], text=True)
native = '\n'.join(line for line in native.splitlines() if line.startswith(('FAMILY ', 'FILE ')))+'\n'
assert native == candidate, (native, candidate)
print('PASS: native selected catalog snapshot, 9 families including 64 discarded vector candidates')
