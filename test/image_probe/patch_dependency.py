"""Apply only the firmware's existing JPEGDEC patches to the host test checkout."""
from pathlib import Path
import subprocess
import sys

source, patches = map(Path, sys.argv[1:])
for patch in sorted(patches.glob('*.patch')):
    command = ['git', 'apply', '--check', '--reverse', str(patch.resolve())]
    if subprocess.run(command, cwd=source, capture_output=True).returncode == 0:
        continue
    subprocess.run(['git', 'apply', '--check', str(patch.resolve())], cwd=source, check=True)
    subprocess.run(['git', 'apply', str(patch.resolve())], cwd=source, check=True)
