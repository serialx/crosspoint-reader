"""Pin private ABI numbers/layouts and reject deliberate enum/layout drift."""
from pathlib import Path
import subprocess
import re
import tempfile
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
INCLUDE = ROOT/'rust/crates/xp-ffi/include'

def enum_value(source, name, value):
    result, count = re.subn(r'('+re.escape(name)+r'\s*=\s*)\d+', lambda m: m[1]+str(value), source)
    assert count == 1, name
    return result

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
            (enum_value(header, 'XP_UI_USB_PREPARING_STATUS', 42), False),
            (header.replace('host_suspend_pending, reserved[2]', 'host_suspend_pending, reserved[6]'), False),
            (enum_value(header, 'XP_UI_CALIBRE_SERVER_BEGIN', 42), False),
            (header.replace('banner_width;', 'banner_width[3];'), False),
            (enum_value(header, 'XP_UI_WEB_SERVER_BEGIN', 42), False),
            (header.replace('wifi_connected, reserved[3]', 'wifi_connected, reserved[7]'), False),
            (enum_value(header, 'XP_UI_WIFI_CALL_SCAN_RSSI', 42),False),
            (header.replace('flash_ticks, ui_invalidated, reserved[2]','flash_ticks, ui_invalidated, reserved[6]'),False)]:
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

with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    calibre = (ROOT/'rust/crates/xp-ui/src/network/calibre.rs').read_text()
    (out/'abi.rs').write_text('#![allow(dead_code)]\nmod model {pub struct Input {pub pressed:u32} pub const BACK:u32=1;}\nmod calibre;\n')
    rust = ['rustc', '--edition=2021', '--crate-type=lib', str(out/'abi.rs'), '-o', str(out/'abi.rlib')]
    for source, okay in [(calibre, True), (calibre.replace('ServerBegin = 10', 'ServerBegin = 42'), False)]:
        (out/'calibre.rs').write_text(source)
        compile_checked(rust, okay)

with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    web = (ROOT/'rust/crates/xp-ui/src/network/web.rs').read_text()
    (out/'abi.rs').write_text('#![allow(dead_code)]\nmod model {pub struct Input {pub pressed:u32,pub held:u32} pub const BACK:u32=1;}\nmod web;\n')
    rust = ['rustc', '--edition=2021', '--crate-type=lib', str(out/'abi.rs'), '-o', str(out/'abi.rlib')]
    for source, okay in [(web, True), (web.replace('ServerBegin = 27', 'ServerBegin = 42'), False),
                         (web.replace('pub start_in_join: u8', 'pub start_in_join: u32'), False)]:
        (out/'web.rs').write_text(source)
        compile_checked(rust, okay)

with tempfile.TemporaryDirectory() as directory:
    out=Path(directory)
    wifi=(ROOT/'rust/crates/xp-ui/src/network/wifi.rs').read_text()
    (out/'abi.rs').write_text('#![allow(dead_code)]\n'+f'#[path="{ROOT}/rust/crates/xp-ui/src/model.rs"] mod model;\n#[path="{ROOT}/rust/crates/xp-ui/src/scene.rs"] mod scene;\nmod wifi;\n')
    rust=['rustc','--edition=2021','--crate-type=lib',str(out/'abi.rs'),'-o',str(out/'abi.rlib')]
    for source,okay in [(wifi,True),(wifi.replace('ScanRssi = 19','ScanRssi = 42'),False),(wifi.replace('pub reserved: [u8; 2]','pub reserved: [u8; 6]'),False)]:
        (out/'wifi.rs').write_text(source)
        compile_checked(rust,okay)
