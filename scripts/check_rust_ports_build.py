"""Validate native Rust ports on every default-X3 build, including cache hits."""

import subprocess
import sys
from pathlib import Path

Import("env")  # noqa: F821 - supplied by PlatformIO/SCons
env = globals()["env"]

# A cold Arduino core build does not compile the firmware objects. PioArduino
# starts a separate firmware pass after the core build completes.
if env.subst("$PIOENV") == "default" and env.subst("$ARDUINO_LIB_COMPILE_FLAG") != "Build":
    project = Path(env.subst("$PROJECT_DIR"))
    mode = env.GetProjectOption("custom_rust_ports", "enforce")
    if mode not in {"report", "enforce"}:
        raise ValueError("custom_rust_ports must be report or enforce")
    toolchain = Path(env.PioPlatform().get_package_dir("toolchain-riscv32-esp"))
    checker = project / "scripts/check_rust_ports.py"
    policy = project / "scripts/rust_ports/policy.txt"
    lock = project / "scripts/rust_ports/ports.lock"
    dynamic_lock = project / "scripts/rust_ports/dynamic.lock"

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
            "--dynamic-lock",
            str(dynamic_lock),
        ]
        if mode == "report":
            command.append("--report")
        return subprocess.call(command, cwd=project)

    program = env.File("$BUILD_DIR/${PROGNAME}.elf")
    check = env.Alias(
        "check_rust_ports",
        program.children(),
        env.VerboseAction(check_ports, "Checking native Rust ports"),
    )
    env.AlwaysBuild(check)
    env.NoCache(check)
    env.Depends(
        check,
        [
            str(checker),
            str(policy),
            str(lock),
            str(dynamic_lock),
            env.Value(mode),
            str(project / "scripts/check_rust_ports_build.py"),
        ],
    )
    # Order-only dependency: validation also runs when ELF is current or cached.
    env.Requires(program, check)
    env.Alias("buildprog", check)
