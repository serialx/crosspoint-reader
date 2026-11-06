"""Compile assertions against the actual C header and extracted Rust wire declarations."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--evidence', type=Path, required=True)
args = parser.parse_args()
out = args.evidence.resolve(); out.mkdir(parents=True, exist_ok=True)
abi = (ROOT/'rust/crates/xp-ffi/src/app/abi.rs').read_text()
wire = abi[abi.index('use core::ffi::c_void;'):abi.index('#[repr(C)]\n#[derive(Clone, Copy)]\npub struct Legacy {')]
checks = r'''
const _: () = {
    let p = core::mem::size_of::<usize>();
    assert!(core::mem::size_of::<DefinitionOwnedText>() == 2*p+8);
    assert!(core::mem::offset_of!(DefinitionOwnedText, data) == 0);
    assert!(core::mem::offset_of!(DefinitionOwnedText, len) == p);
    assert!(core::mem::offset_of!(DefinitionOwnedText, capacity) == p+4);
    assert!(core::mem::offset_of!(DefinitionOwnedText, release) == p+8);
    assert!(core::mem::offset_of!(DefinitionSelection, start_page) == 2*p+8);
    assert!(core::mem::offset_of!(DefinitionSelection, end_page) == 2*p+10);
    assert!(core::mem::offset_of!(DefinitionSelection, start_word) == 2*p+12);
    assert!(core::mem::offset_of!(DefinitionSelection, end_word) == 2*p+14);
    assert!(core::mem::offset_of!(DefinitionSelection, status) == 2*p+16);
    assert!(core::mem::offset_of!(DefinitionSelection, reserved) == 2*p+17);
    assert!(core::mem::size_of::<DefinitionSelection>() == if p==4 {28} else {40});
};
'''
(out/'layout.rs').write_text('#![no_std]\n'+wire+checks)
cpp = r'''
#include <xp_app.h>
constexpr auto p = sizeof(void*);
static_assert(sizeof(XpAppLegacy) == 17*p);
static_assert(offsetof(XpAppLegacy, with_definition_selection_result) == 14*p);
static_assert(offsetof(XpAppLegacy, deliver_definition_selection_result) == 15*p);
static_assert(offsetof(XpAppLegacy, definition_selection_install_failed) == 16*p);
static_assert(sizeof(XpDefinitionOwnedText) == 2*p+8);
static_assert(offsetof(XpDefinitionOwnedText, data) == 0);
static_assert(offsetof(XpDefinitionOwnedText, len) == p);
static_assert(offsetof(XpDefinitionOwnedText, capacity) == p+4);
static_assert(offsetof(XpDefinitionOwnedText, release) == p+8);
static_assert(offsetof(XpDefinitionSelection, text) == 0);
static_assert(offsetof(XpDefinitionSelection, start_page) == 2*p+8);
static_assert(offsetof(XpDefinitionSelection, end_page) == 2*p+10);
static_assert(offsetof(XpDefinitionSelection, start_word) == 2*p+12);
static_assert(offsetof(XpDefinitionSelection, end_word) == 2*p+14);
static_assert(offsetof(XpDefinitionSelection, status) == 2*p+16);
static_assert(offsetof(XpDefinitionSelection, reserved) == 2*p+17);
static_assert(sizeof(XpDefinitionSelection) == (p==4 ? 28 : 40));
'''
(out/'layout.cpp').write_text(cpp)
commands = []
for target in ['host', 'c3']:
    args = ['rustc','--crate-type','lib','--edition','2021','--emit=metadata',str(out/'layout.rs'),'-o',str(out/(target+'.rmeta'))]
    if target == 'c3': args += ['--target','riscv32imc-unknown-none-elf']
    commands.append(args)
    subprocess.run(args,check=True)
    compiler = 'c++' if target == 'host' else '/Users/serialx/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-g++'
    args = [compiler,'-std=c++20','-Wall','-Wextra','-Werror','-I'+str(ROOT/'rust/crates/xp-ffi/include'),'-c',str(out/'layout.cpp'),'-o',str(out/(target+'.o'))]
    commands.append(args)
    subprocess.run(args,check=True)
(out/'results.json').write_text(json.dumps({'commands':commands, 'cpp_header_sha256':hashlib.sha256((ROOT/'rust/crates/xp-ffi/include/xp_app.h').read_bytes()).hexdigest(), 'rust_abi_sha256':hashlib.sha256(abi.encode()).hexdigest(),'outcome':'C3 and host C/Rust layout assertions passed'},indent=2)+'\n')
print('C3 and host C/Rust layout assertions passed')
