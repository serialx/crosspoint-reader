"""Keep historical oracle headers out of the resolved C/C++ include paths."""
import argparse
import json
from pathlib import Path
import shlex


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
    for command in commands:
        arguments = command.get("arguments")
        if arguments is None:
            arguments = shlex.split(command["command"])
        for value in include_paths(arguments):
            path = Path(command["directory"]) / value
            if "native_oracle" in path.resolve().parts:
                failures.append(f'{command["file"]}: {value}')
    if failures:
        raise SystemExit("native_oracle must not be an include path:\n" + "\n".join(failures))
    print(f"Checked {len(commands)} C/C++ compile commands: oracle include paths absent")


if __name__ == "__main__":
    main()
