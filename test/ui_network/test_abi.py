"""Pin private ABI numbers/layouts and reject deliberate enum/layout drift."""
from pathlib import Path
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
INCLUDE = ROOT/'rust/crates/xp-ffi/include'

def compile_checked(command, okay):
    result = subprocess.run(command, capture_output=True, text=True)
    if (result.returncode == 0) != okay:
        raise AssertionError(result.stderr)
    if not okay:
        assert 'assert' in result.stderr, result.stderr

with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    header = (INCLUDE/'xp_ui_network.h').read_text()
    cpp = ['c++', '-std=c++20', '-fsyntax-only', '-I'+str(out), '-I'+str(INCLUDE), str(HERE/'abi.cpp')]
    for source, okay in [(header, True),
            (header.replace('XP_UI_USB_PREPARING_STATUS = 11', 'XP_UI_USB_PREPARING_STATUS = 42'), False),
            (header.replace('host_suspend_pending, reserved[2]', 'host_suspend_pending, reserved[6]'), False)]:
        (out/'xp_ui_network.h').write_text(source)
        compile_checked(cpp, okay)
    usb = (ROOT/'rust/crates/xp-ui/src/network/usb.rs').read_text()
    (out/'abi.rs').write_text('''#![allow(dead_code)]
mod model { pub struct Input {pub pressed: u32} pub const BACK: u32=1; pub const POWER: u32=256; }
mod usb;
''')
    rust = ['rustc', '--edition=2021', '--crate-type=lib', str(out/'abi.rs'), '-o', str(out/'abi.rlib')]
    for source, okay in [(usb, True),
            (usb.replace('PreparingStatus = 11', 'PreparingStatus = 42'), False),
            (usb.replace('reserved: [u8; 2]', 'reserved: [u8; 6]'), False)]:
        (out/'usb.rs').write_text(source)
        compile_checked(rust, okay)
