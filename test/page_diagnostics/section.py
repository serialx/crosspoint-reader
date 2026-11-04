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
import json
import time

binary = Path(sys.argv[1]).resolve()
capture = binary.parent / f'section-diagnostics-{time.time_ns()}'
capture.mkdir()
construction = [
    'ERR\tEBP\tCould not find or size META-INF/container.xml',
    'ERR\tEBP\tCould not find content.opf in zip',
    'ERR\tEBP\tCould not parse content.opf from cached bookMetadata for CSS files',
]
for case, message in expected.items():
    with tempfile.TemporaryDirectory() as tmp:
        p = subprocess.run([str(binary), case], cwd=tmp, capture_output=True, text=True)
    (capture / (case + '.json')).write_text(json.dumps({
        'status': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr,
    }, indent=2) + '\n')
    assert p.returncode == 0, (case, p.returncode, p.stderr)
    assert p.stdout.count('ADMISSION 1\n') == 1, (case, 'admission failed', p.stdout)
    admission, section = p.stdout.split('ADMISSION 1\n')
    logs = lambda segment: [line for line in segment.splitlines() if line.startswith(('ERR\t', 'INF\t'))]
    assert logs(admission) == construction, (case, 'construction diagnostics changed', p.stdout)
    assert logs(section) == ['ERR\tSCT\t' + message], (case, p.stdout)
    assert [line for line in section.splitlines() if line.startswith('RESULT ')] == ['RESULT 0'], (case, p.stdout)
print(f'4 unchanged Section diagnostics and parent-matched construction diagnostics; raw captures {capture}')
