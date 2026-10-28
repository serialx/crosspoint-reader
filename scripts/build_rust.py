"""Build the Rust C ABI archive before PlatformIO resolves linker inputs."""

import filecmp
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

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

command += ["build", "--locked", "--release", "-p", "xp-ffi", "--target", target]
defines = env.ParseFlags(env.get("BUILD_FLAGS", [] )).get("CPPDEFINES", [])
if any(d == "FREEINK_RUST_PANEL" or
       (isinstance(d, (tuple, list)) and d[0] == "FREEINK_RUST_PANEL" and str(d[1]) != "0")
       for d in defines):
    command += ["--features", "display-panel"]
if mcu == "esp32s3":
    # Building the sysroot needs registry crates, even with only local dependencies.
    command += ["-Zbuild-std=core,alloc"]
else:
    command += ["--offline"]

process_env = os.environ.copy()
if mcu == "esp32c3":
    # Preserve offline Rust backtraces in non-flashed DWARF before filtering EH.
    flag = "-Cllvm-args=--force-dwarf-frame-section"
    if "CARGO_ENCODED_RUSTFLAGS" in process_env:
        process_env["CARGO_ENCODED_RUSTFLAGS"] = "\x1f".join(
            value for value in (process_env["CARGO_ENCODED_RUSTFLAGS"], flag) if value
        )
    else:
        process_env["RUSTFLAGS"] = process_env.get("RUSTFLAGS", "") + " " + flag
process_env["CARGO_TARGET_DIR"] = str(target_dir)
# Setup is explicit: rustup must not fetch a missing pinned toolchain during pio.
process_env["RUSTUP_AUTO_INSTALL"] = "0"
try:
    subprocess.run(command, cwd=workspace, env=process_env, check=True)
except (OSError, subprocess.CalledProcessError) as error:
    fail(f"{error}; install the required toolchain first (docs/contributing/rust.md)")

library_dir = target_dir / target / "release"
# Prebuilt C3 core/alloc retain unwind metadata through LTO despite panic=abort.
# Keep Cargo's archive intact; only its LTO object is filtered in the link copy.
# Prebuilt compiler_builtins keep their original EH metadata.
if mcu == "esp32c3":
    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"))
    config = (framework / mcu / "sdkconfig").read_text()
    if "# CONFIG_ESP_SYSTEM_USE_EH_FRAME is not set" in config:
        toolchain = Path(env.PioPlatform().get_package_dir("toolchain-riscv32-esp"))
        objcopy = shutil.which("riscv32-esp-elf-objcopy", path=str(toolchain / "bin"))
        ar = shutil.which("riscv32-esp-elf-ar", path=str(toolchain / "bin"))
        if objcopy is None or ar is None:
            fail("target objcopy/ar is missing")
        archive = library_dir / "libxp_ffi.a"
        library_dir = library_dir / "firmware"
        library_dir.mkdir(exist_ok=True)
        temporary = library_dir / "libxp_ffi.tmp.a"
        filtered = library_dir / "libxp_ffi.a"
        try:
            members = subprocess.check_output([ar, "t", str(archive)], text=True).splitlines()
            lto = [name for name in members if name.startswith("xp_ffi-") and name.endswith(".rcgu.o")]
            if len(lto) != 1:
                fail("expected one xp-ffi LTO object; review unwind filtering for this toolchain")
            shutil.copyfile(archive, temporary)
            with tempfile.TemporaryDirectory(dir=library_dir) as scratch:
                member = Path(scratch) / lto[0]
                member.write_bytes(subprocess.check_output([ar, "p", str(archive), lto[0]]))
                subprocess.run([objcopy, "--remove-section=.eh_frame", "--remove-section=.eh_frame_hdr",
                                str(member)], check=True)
                subprocess.run([ar, "rD", str(temporary), str(member)], check=True)
            if filtered.exists() and filecmp.cmp(temporary, filtered, shallow=False):
                temporary.unlink()
            else:
                temporary.replace(filtered)
        except (OSError, subprocess.CalledProcessError) as error:
            fail(f"cannot filter Rust unwind metadata: {error}")
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
