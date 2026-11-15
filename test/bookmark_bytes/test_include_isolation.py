"""Check the byte runner's compiler command, including renamed materializations."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CHECK = ROOT / 'test/cmake/check_native_oracle_includes.py'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('commands', type=Path)
    args = parser.parse_args()
    commands = json.loads(args.commands.read_text())
    subprocess.run([sys.executable, str(CHECK), str(args.commands)], check=True)
    with tempfile.TemporaryDirectory(prefix='bookmark-isolation-') as directory:
        out = Path(directory)
        manifest = json.loads((HERE/'native_oracle/manifest.json').read_text())
        for row in manifest['snapshots']:
            target = out/'materialized'/row['source']
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((HERE/'native_oracle'/row['snapshot']).read_bytes())
        for include in [HERE/'native_oracle', out/'materialized', out/'materialized/src']:
            bad = [dict(command, arguments=[*command['arguments'], '-I'+str(include)]) for command in commands]
            path = out/'commands.json'
            path.write_text(json.dumps(bad))
            result = subprocess.run([sys.executable, str(CHECK), str(path)], capture_output=True, text=True)
            assert result.returncode != 0, f'accepted historical include root: {include}'
            print(f'Rejected historical include root: {include}')


if __name__ == '__main__':
    main()
