"""Build the Rust C ABI archive before PlatformIO resolves linker inputs."""

import os
from pathlib import Path
import shutil
import subprocess
import sys

Import("env")


def fail(message):
    print(f"Rust build: {message}", file=sys.stderr)
    env.Exit(1)


cargo = shutil.which("cargo")
if cargo is None:
    fail("cargo is missing from PATH; install the toolchain in docs/contributing/rust.md")

project = Path(env.subst("$PROJECT_DIR"))
workspace = project / "rust"
target_dir = Path(env.subst("$BUILD_DIR")) / "rust"
mcu = env.BoardConfig().get("build.mcu")
command = [cargo]
if mcu == "esp32c3":
    target = "riscv32imc-unknown-none-elf"
elif mcu == "esp32s3":
    target = "xtensa-esp32s3-none-elf"
    rustup = shutil.which("rustup")
    if rustup is None or subprocess.run(
        [rustup, "run", "esp", "rustc", "--version"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode != 0:
        fail("ESP32-S3 requires the esp toolchain; install espup and run espup install")
    command += ["+esp"]
else:
    fail(f"unsupported MCU {mcu!r}; no Rust target is configured")

command += ["build", "--locked", "--offline", "--release", "-p", "xp-ffi", "--target", target]
if mcu == "esp32s3":
    command += ["-Zbuild-std=core,alloc"]

process_env = os.environ.copy()
process_env["CARGO_TARGET_DIR"] = str(target_dir)
# Setup is explicit: rustup must not fetch a missing pinned toolchain during pio.
process_env["RUSTUP_AUTO_INSTALL"] = "0"
try:
    subprocess.run(command, cwd=workspace, env=process_env, check=True)
except (OSError, subprocess.CalledProcessError) as error:
    fail(f"{error}; install the required toolchain first (docs/contributing/rust.md)")

library_dir = target_dir / target / "release"
env.Append(LIBPATH=[str(library_dir)], LIBS=["xp_ffi"])
# PlatformIO adds framework libraries after pre-scripts. Order at expansion time
# so libm/libgcc resolve existing firmware calls before Rust's weak builtins.
original_libflags = env["_LIBFLAGS"]


def rust_libflags(target, source, env, for_signature):
    libraries = [library for library in env["LIBS"] if library != "xp_ffi"]
    link_env = env.Override({"LIBS": libraries + ["xp_ffi"]})
    return link_env.subst(original_libflags, target=target, source=source)


env["_RUST_LIBFLAGS"] = rust_libflags
env["_LIBFLAGS"] = "${_RUST_LIBFLAGS}"
env.Append(CPPPATH=[str(workspace / "crates" / "xp-ffi" / "include")])
# Track the real archive so Rust-only edits relink the ELF without a stamp file.
env.Depends("$BUILD_DIR/${PROGNAME}.elf", str(library_dir / "libxp_ffi.a"))
