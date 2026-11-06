"""Production App/native Definition result transfer with traced physical Page/lock witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/reader_lifecycle'))
sys.path.insert(0, str(ROOT / 'test'))
from test_app_results import block
from rust_host import rust_link_flags


def source():
    viewer = (ROOT / 'src/activities/reader/DictionaryDefinitionActivity.cpp').read_text()
    header = (ROOT / 'src/activities/reader/DictionaryDefinitionActivity.h').read_text()
    manager = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
    owner = (ROOT / 'src/activities/OwnedLegacyActivity.h').read_text()
    selector = (ROOT / 'src/activities/reader/ClipSelectionActivity.h').read_text()
    text = Path(__file__).with_name('composition.cpp.in').read_text()
    replacements = {
        'SELECTION': block(selector, 'struct Selection {') + ';',
        'STRUCTS': '\n'.join(block(header, 'struct ' + name + ' {') + ';' for name in ['Line', 'BodyArea', 'Definition', 'HistoryEntry', 'LookupState']),
        'OWNED_NATIVE_DECLARATION': block(owner, 'struct OwnedLegacyActivity {') + ';',
        'NATIVE_ACTIVITY_GETTER': block(owner, 'Activity* nativeActivity(const void* object)'),
        'PREPARATION': block(viewer, 'XpDefinitionSelection DictionaryDefinitionActivity::prepareSelectionResult').replace('  if (lookup->depth', '  record("decision");\n  if (lookup->depth'),
        'ACCEPTANCE': block(viewer, 'uint8_t DictionaryDefinitionActivity::acceptSelectionResult'),
        'START_SELECTION': block(viewer, '__attribute__((noinline)) void DefinitionQuery::assignNative') + '\n' + block(viewer, 'void DictionaryDefinitionActivity::startSelection'),
        'FAILED_INSTALL': block(viewer, 'void DictionaryDefinitionActivity::selectionInstallFailed'),
        'BRIDGE': block(manager, '[](void* child, XpDefinitionPop pop, XpDefinitionSelectionSink sink, void* context)').replace('auto result =', 'ResultLifetime lifetime;\n            auto result =').replace('void* parent =', 'record("native-take");\n            void* parent ='),
        'NATIVE_ACCEPT': block(manager, '[](void* parent, XpDefinitionSelection result)'),
        'NATIVE_FAILED': block(manager.split('.definition_selection_install_failed =', 1)[1], '[](void* parent)'),
    }
    for marker, value in replacements.items():
        assert text.count('// ' + marker + '\n') == 1, marker
        text = text.replace('// ' + marker + '\n', value + '\n')
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    candidate = args.evidence / 'composition.cpp'
    candidate.write_text(source())
    binary = args.evidence / 'composition'
    compile_command = ['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-DXP_FFI_HOST_FAULT_INJECTION', '-I'+str(ROOT/'src'), '-I'+str(ROOT/'lib/Memory'), '-I'+str(ROOT/'test/ui_runtime'),
                       '-I'+str(ROOT/'test/reader_lifecycle'), str(candidate), *rust_link_flags(str(args.archive.resolve())), '-o', str(binary)]
    result = subprocess.run(compile_command, text=True, capture_output=True)
    (args.evidence/'compile.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        print(result.stderr[-6000:]); raise SystemExit(result.returncode)
    cases = [('success', length, depth, cancelled) for length in [0, 1, 15, 4096] for depth in [15,16] for cancelled in [0,1]]
    cases += [(mode, 4096, 15, 0) for mode in ['heap','allocation','decline','decline-sink','retained-failure','reenter-pop','child-allocation','stack-failure','home-current','home-retained','reenter']]
    cases.append(('retained-reuse', 1, 15, 0))
    records = []
    for case in cases:
        result = subprocess.run([str(binary), *map(str, case)], text=True, capture_output=True)
        label = '-'.join(map(str,case))
        (args.evidence/(label+'.log')).write_text(result.stdout + result.stderr)
        records.append({'case':case, 'exit':result.returncode})
        if result.returncode:
            print(label, result.stderr, result.stdout); break
    (args.evidence/'results.json').write_text(json.dumps({'command':compile_command, 'source_sha256':hashlib.sha256(candidate.read_bytes()).hexdigest(), 'cases':records},indent=2)+'\n')
    assert len(records) == len(cases) and all(r['exit']==0 for r in records), records[-1]
    print(f'{len(records)} native/production App composition cases passed')

if __name__ == '__main__': main()
