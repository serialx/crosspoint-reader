"""Keep historical oracle headers out of the resolved C/C++ include paths."""
import argparse
import hashlib
from functools import lru_cache
import json
from pathlib import Path
import shlex

ROOT = Path(__file__).resolve().parents[2]


def historical_headers():
    for manifest in (ROOT / 'test').glob('*/native_oracle/manifest.json'):
        for row in json.loads(manifest.read_text()).get('snapshots', []):
            if not isinstance(row.get('source'), str):
                continue  # Composite generated fixtures have no production header path.
            source = Path(row['source'])
            if source.suffix in ('.h', '.hpp'):
                yield source, row['sha256']


@lru_cache(maxsize=None)
def materialized_header(root, headers):
    for source, digest in headers:
        for start in range(len(source.parts)):
            candidate = root / Path(*source.parts[start:]) if root.is_dir() else root
            if candidate.resolve() == (ROOT / source).resolve():
                continue
            if candidate.is_file() and hashlib.sha256(candidate.read_bytes()).hexdigest() == digest:
                return candidate
    return None


def include_paths(arguments):
    flags = ("-I", "-isystem", "-iquote", "-idirafter", "-include", "-imacros", "/I", "/FI")
    args = iter(arguments)
    for arg in args:
        if arg in flags:
            yield next(args, "")
        else:
            for flag in flags:
                if arg.startswith(flag) and len(arg) > len(flag):
                    yield arg[len(flag):]
                    break


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("commands", type=Path)
    args = parser.parse_args()
    commands = json.loads(args.commands.read_text())
    if not commands:
        raise SystemExit("compile_commands.json contains no C/C++ commands")
    failures = []
    headers = tuple(historical_headers())
    for command in commands:
        arguments = command.get("arguments")
        if arguments is None:
            arguments = shlex.split(command["command"])
        for value in include_paths(arguments):
            path = Path(command["directory"]) / value
            if "native_oracle" in path.resolve().parts:
                failures.append(f'{command["file"]}: {value}')
            elif materialized_header(path, headers):
                failures.append(f'{command["file"]}: materialized historical header under {value}')
    if failures:
        raise SystemExit("native_oracle must not be an include path:\n" + "\n".join(failures))
    print(f"Checked {len(commands)} C/C++ compile commands: oracle include paths absent")


if __name__ == "__main__":
    main()
