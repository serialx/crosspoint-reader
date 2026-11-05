#!/usr/bin/env python3
"""Allocation diagnostics must survive milestones and reject missing OOM events."""
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile

from diagnostics import gif
from png_cases import png


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    build = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        for kind, data in [('png', png()), ('gif', gif.SMALL)]:
            path = Path(tmp) / ('input.' + kind)
            path.write_bytes(data)
            command = [str(build / (kind + '_resources')), str(path)]
            env = dict(os.environ, VERIFY_DIAGNOSTICS='1')
            subprocess.run(command, env=env, check=True, capture_output=True)
            negative = subprocess.run(command, env=dict(env, DROP_ALLOCATION_DIAGNOSTICS='1'),
                                      capture_output=True)
            assert negative.returncode != 0
            assert b'Missing allocation diagnostic' in negative.stderr
    print('PNG/GIF allocation failures require error events; milestones cannot satisfy the check')


if __name__ == '__main__':
    main()
