#!/usr/bin/env python3
"""Compare decoder allocation/failure traces with a separately linked baseline."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
from png_cases import png, transition_cases

HERE = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=Path)
    args = parser.parse_args()
    cases = {f'png-{color}-{depth}.png': png(color=color, depth=depth)
             for color, depth in ((0, 1), (0, 8), (0, 16), (2, 8), (2, 16), (3, 4), (3, 8), (4, 8), (6, 8), (6, 16))}
    cases.update({name+'.png': data for name, data in transition_cases().items()
                  if name.startswith(('leading-', 'bytewise-'))})
    import importlib.util
    spec = importlib.util.spec_from_file_location('gif_oracle', HERE.parent / 'gif_support/oracle.py')
    gif = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gif)
    cases.update({name+'.gif': data for name, data in gif.fixtures().items()
                  if name in ('small', 'local-palette', 'extension-254', 'early-eoi', 'zero-frame')})
    env = dict(os.environ, IMAGE_ALLOCATION_TRACE='1')
    with tempfile.TemporaryDirectory(prefix='image-resources-') as tmp:
        for name, data in cases.items():
            path = Path(tmp) / name
            path.write_bytes(data)
            kind = path.suffix[1:]
            before = subprocess.check_output([str(args.build / f'{kind}_resources_base'), str(path)], env=env)
            after = subprocess.check_output([str(args.build / f'{kind}_resources'), str(path)],
                                            env=dict(env, VERIFY_DIAGNOSTICS='1'))
            assert before == after, f'allocation trace changed: {name}'
            print(name, len(before.splitlines()), hashlib.sha256(before).hexdigest(), flush=True)

if __name__ == '__main__':
    main()
