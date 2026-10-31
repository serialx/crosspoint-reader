"""Link standalone C++ regression harnesses to the production Rust facade."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def rust_link_flags():
    archive = os.environ.get('XP_TEST_RUST_ARCHIVE')
    if archive is None:
        target = ROOT / 'build/rust-host'
        subprocess.run(['cargo', 'build', '--manifest-path', str(ROOT / 'rust/Cargo.toml'),
                        '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked'], check=True)
        archive = str(target / 'release/libxp_ffi.a')
    return ['-I', str(ROOT / 'rust/crates/xp-ffi/include'),
            str(ROOT / 'test/cmake/xp_ffi_host.cpp'), archive]
