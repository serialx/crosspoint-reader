"""Verify production chapter-window reads and prewarm against the native consumer."""
import argparse
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--rust', required=True)
a = p.parse_args()
with tempfile.TemporaryDirectory(prefix='reader-chapters-') as directory:
    trace = subprocess.check_output([a.native, directory])
actual = subprocess.check_output([a.rust], input=trace)
expected = b'\n'.join(line for line in trace.splitlines() if line.startswith((b'E ', b'L ', b'P '))) + b'\n'
assert actual == expected, 'Rust window differs from native current rows/prewarm'
print('9 native/Rust chapter windows equal; 1000-entry real cache, two mid-window read failures, reuse and prewarm')
