"""Reject ownership/order mutants without editing production or copying book fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT/'test'))
from composition import source
from rust_host import rust_link_flags


def once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    out = args.evidence.resolve(); out.mkdir(parents=True, exist_ok=True)
    records = []
    def run(command, label, expected):
        result = subprocess.run(command, capture_output=True, text=True, env={**os.environ, 'MIRIFLAGS':'-Zmiri-strict-provenance'}, timeout=240)
        (out/(label+'.log')).write_text(result.stdout+result.stderr)
        records.append({'label':label,'command':list(map(str,command)),'exit':result.returncode,'expected':expected})
        (out/'results.json').write_text(json.dumps(records,indent=2)+'\n')
        if expected == 'success': assert result.returncode == 0, (label, result.stderr[-3000:])
        elif expected == 'miri-ub': assert result.returncode != 0 and 'error: Undefined Behavior' in result.stderr, (label, result.stderr[-4000:])
        else: assert result.returncode != 0 and expected in result.stderr, (label, result.stderr[-4000:])
    native = source()
    mutations = {
        'early-allocation': once(native, 'void* parent = pop(context);', 'auto* early = allocateText(1); freeText(early);\n            void* parent = pop(context);'),
        'early-page-release': once(native, 'void* parent = pop(context);', 'static_cast<DictionaryDefinitionActivity*>(nativeActivity(parentHandle))->lookup->selectionPages.clear();\n            void* parent = pop(context);'),
    }
    for name, content in mutations.items():
        cpp = out/(name+'.cpp'); cpp.write_text(content)
        binary = out/name
        command = ['c++','-std=c++20','-UNDEBUG','-O0','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DXP_FFI_HOST_FAULT_INJECTION',
                   '-I'+str(ROOT/'src'),'-I'+str(ROOT/'lib/Memory'),'-I'+str(ROOT/'test/ui_runtime'),'-I'+str(ROOT/'test/reader_lifecycle'),
                   str(cpp),*rust_link_flags(str(args.archive.resolve())),'-o',str(binary)]
        run(command,name+'-compile','success')
        run([str(binary),'success','4096','15','0'],name,'Assertion failed:')
    harness = out/'miri-harness'; harness.mkdir(exist_ok=True)
    for original in (ROOT/'test/reader_children').iterdir():
        if original.is_file() and original.suffix in ['.rs','.toml','.lock']:
            content = original.read_text().replace('../../rust/',str(ROOT/'rust')+'/')
            (harness/original.name).write_text(content)
    definition = (ROOT/'rust/crates/xp-ffi/src/app/epub/children/definition.rs').read_text()
    tests = (harness/'definition_tests.rs').read_text()
    lib = (harness/'lib.rs').read_text().replace(str(ROOT/'rust/crates/xp-ffi/src/app/epub/children/definition.rs'),str(harness/'definition.rs'))
    (harness/'lib.rs').write_text(lib)
    native_stack = once(tests,'        let length = TRANSFER_LENGTH.with(Cell::get);','        let length = TRANSFER_LENGTH.with(Cell::get);\n        let mut stack_text = [b\'x\'; 4097];')
    native_stack = once(native_stack,'std::alloc::alloc(std::alloc::Layout::array::<u8>(length + 1).unwrap())','stack_text.as_mut_ptr()')
    double = once(tests,'if unsafe { sink(context, parent, result) } == 0 && !data.is_null() {','if unsafe { sink(context, parent, result) } <= 1 && !data.is_null() {')
    protected = once(definition,'        let mut context = Context {','''        fn protect<T>(owner: &DefinitionData, run: impl FnOnce() -> T) -> T {
            let value = run();
            core::hint::black_box(owner);
            value
        }
        let mut context = Context {''')
    protected = once(protected,'        unsafe {\n            bridge(','        unsafe {\n            protect(child.cast::<DefinitionData>().as_ref(), || bridge(')
    protected = once(protected,'                (&mut context as *mut Context<P, D>).cast(),\n            );','                (&mut context as *mut Context<P, D>).cast(),\n            ));')
    for name, implementation, witness in [('control',definition,tests),('retained-native-stack-address',definition,native_stack),('double-free',definition,double),('whole-owner-protector',protected,tests)]:
        (harness/'definition.rs').write_text(implementation)
        (harness/'definition_tests.rs').write_text(witness)
        (out/(name+'-definition.rs')).write_text(implementation)
        (out/(name+'-witness.rs')).write_text(witness)
        run(['cargo','+nightly','miri','test','--manifest-path',str(harness/'Cargo.toml'),'--lib','definition_transfer','--','--test-threads=1'],name,'success' if name=='control' else 'miri-ub')
    (harness/'definition.rs').write_text(definition)
    (harness/'definition_tests.rs').write_text(tests)
    print('Five required negative controls rejected; unmodified Miri control passed')

if __name__ == '__main__': main()
