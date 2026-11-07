"""Run isolated production-owner mutants; each named fault must be rejected."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--evidence', type=Path, required=True)
args = parser.parse_args()
out = args.evidence.resolve()
out.mkdir(parents=True, exist_ok=False)
source = out / 'src'
shutil.copytree(ROOT / 'rust/crates/xp-ffi/src', source)
manifest = out / 'Cargo.toml'
manifest.write_text('[package]\nname="definition-negative"\nversion="0.0.0"\nedition="2021"\n'
                    '[workspace]\n[lib]\npath="lib.rs"\n[features]\napp-test=[]\n[dependencies]\n'
                    + ''.join(f'{name}={{path="{ROOT / "rust/crates" / name}"}}\n' for name in ['xp-core', 'xp-ui']))
lib = (ROOT / 'test/home_ui/app_owner/lib.rs').read_text()
(out / 'lib.rs').write_text(lib.replace('../../../rust/crates/xp-ffi/src/', str(source) + '/'))
children = source / 'app/epub/children'
protected = '''
#[test]
fn protected_whole_owner_negative() {
    #[inline(never)]
    fn protected(owner: &super::definition_owned::DefinitionSession, destroy: impl FnOnce()) {
        destroy();
        std::hint::black_box(owner);
    }
    let owner = owner();
    let target = owner.pointer();
    protected(unsafe { target.as_ref() }, || drop(owner));
}
'''
mutations = [
    ('frontlight-filter-removed', '../../definition.rs', '.filter(|result| result.kind == abi::RESULT_CLIPPING)', '', 'frontlight_return', False),
    ('control', 'definition_session_tests.rs', None, None, 'stable_owner_moves', True),
    ('whole-owner-protector', 'definition_session_tests.rs', '', protected, 'protected_whole_owner_negative', False),
    ('wrong-heap', 'definition_navigation.rs', '(self.physical.heap)(&mut heap)', '(leaves.heap_snapshot)(&mut heap)', 'default_heap_not_internal', False),
    ('failed-view-published', 'definition_owned.rs', 'return None;\n        }\n        Some(unsafe { out.assume_init() })', 'return Some(unsafe { out.assume_init() });\n        }\n        Some(unsafe { out.assume_init() })', 'failed_prepare_sentinel', False),
]
records = []
for name, file, before, after, test, success in mutations:
    path = children / file
    original = path.read_text()
    changed = original if before is None else original + after if not before else original.replace(before, after)
    assert before is None or changed != original
    path.write_text(changed)
    cmd = ['cargo', '+nightly', 'miri', 'test', '--offline', '--manifest-path', str(manifest), '--features', 'app-test', test]
    run = subprocess.run(cmd, env={**os.environ, 'MIRIFLAGS': '-Zmiri-strict-provenance'}, text=True, capture_output=True)
    log = run.stdout + run.stderr
    (out / (name + '.log')).write_text(log)
    (out / (name + '.rs')).write_text(changed)
    path.write_text(original)
    assert (run.returncode == 0) == success, name
    assert success or ('Undefined Behavior' in log if name == 'whole-owner-protector' else 'assertion' in log), name
    records.append({'name': name, 'command': cmd, 'exit': run.returncode, 'expected_success': success})
(out / 'results.json').write_text(json.dumps(records, indent=2) + '\n')
print('Control passed; frontlight filter, whole-owner protector, wrong heap and failed-view mutants rejected')
