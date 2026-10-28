#!/usr/bin/env python3
import subprocess
import sys
import tempfile

expected = {
    'version': 'Deserialization failed: Unknown version 255',
    'parameters': 'Deserialization failed: Parameters do not match',
    'seek': 'Failed to seek visible-text LUT',
    'read': 'Failed to read visible-text LUT batch',
}
from pathlib import Path
binary = str(Path(sys.argv[1]).resolve())
for case, message in expected.items():
    with tempfile.TemporaryDirectory() as tmp:
        p = subprocess.run([binary, case], cwd=tmp, capture_output=True, text=True, check=True)
    logs = [line for line in p.stdout.splitlines() if line.startswith(('ERR\t','INF\t'))]
    assert logs == ['ERR\tSCT\t' + message], (case, p.stdout)
    assert 'RESULT 0' in p.stdout, (case, p.stdout)
print('4 section failure diagnostics matched exactly')
