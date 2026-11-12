"""Register the native Rust-port check before the default-X3 final link."""

import subprocess
import sys
from pathlib import Path

Import("env")  # noqa: F821 - supplied by PlatformIO/SCons
env = globals()["env"]

if env.subst("$PIOENV") == "default":
    project = Path(env.subst("$PROJECT_DIR"))
    mode = env.GetProjectOption("custom_rust_ports", "enforce")
    if mode not in {"report", "enforce"}:
        raise ValueError("custom_rust_ports must be report or enforce")
    toolchain = Path(env.PioPlatform().get_package_dir("toolchain-riscv32-esp"))
    checker = project / "scripts/check_rust_ports.py"
    policy = project / "scripts/rust_ports/policy.txt"
    lock = project / "scripts/rust_ports/ports.lock"

    def check_ports(source, target, env):
        command = [
            env.subst("$PYTHONEXE") or sys.executable,
            str(checker),
            "--build-dir",
            env.subst("$BUILD_DIR"),
            "--source",
            str(project),
            "--tool-prefix",
            str(toolchain / "bin/riscv32-esp-elf-"),
            "--policy",
            str(policy),
            "--lock",
            str(lock),
        ]
        if mode == "report":
            command.append("--report")
        return subprocess.call(command, cwd=project)

    env.Depends(
        "$BUILD_DIR/${PROGNAME}.elf",
        [
            str(checker),
            str(policy),
            str(lock),
            env.Value(mode),
            str(project / "scripts/check_rust_ports_build.py"),
        ],
    )
    env.AddPreAction(
        "$BUILD_DIR/${PROGNAME}.elf",
        env.VerboseAction(check_ports, "Checking native Rust ports"),
    )
