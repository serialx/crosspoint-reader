"""Build through the actual reader Section and compare frozen CP2 cache bytes."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import struct
import shutil
import zipfile
from book_section_composition import section_source, section_command, section_fixture, section_owner_source
from test_popup_resource import definition, replace_once, native_leaf
ROOT = Path(__file__).resolve().parents[2]


def main():
    source = section_owner_source(ROOT) + (ROOT / 'test/reader_lifecycle/section_driver_oracle.cpp').read_text()
    output = Path(os.environ.get('XP_SECTION_DRIVER_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='section-driver-'))
    output.mkdir(parents=True, exist_ok=True)
    cpp, binary = output / 'oracle.cpp', output / 'oracle'
    cpp.write_text(source)
    book, cache = section_fixture(output)
    rich = output / 'rich'
    rich.mkdir(exist_ok=True)
    rich_book, rich_cache = section_fixture(rich)
    html = (b"<!DOCTYPE html SYSTEM 'unused'><html><body><p id='anchor'>Start &nbsp; " +
            b'&' + b'x' * 1100 + b';</p>' +
            b'<table><tr><td>first cell</td><td>second cell</td></tr></table>' * 5 +
            b'<p>Text before image.</p><img src="../tiny%2Epng"/><img src="unsupported.svg"/>' +
            b'<p>Body text after table and image, with enough lines for several pages.</p>' * 80 +
            b'</body></html>')
    with zipfile.ZipFile(rich_book, 'w', compression=zipfile.ZIP_STORED) as archive:
        archive.writestr('a.xhtml', html)
        archive.writestr('b.xhtml', b'image!')
        archive.writestr('tiny.png', b'\x89PNG\r\n\x1a\n' + struct.pack('>I', 13) + b'IHDR' + struct.pack('>II', 2, 3))
    metadata = bytearray((rich_cache / 'book.bin').read_bytes())
    table = struct.unpack_from('<I', metadata, 1)[0]
    for index, cumulative in enumerate((len(html), len(html) + 6)):
        row = struct.unpack_from('<I', metadata, table + index * 4)[0]
        length = struct.unpack_from('<I', metadata, row)[0]
        struct.pack_into('<I', metadata, row + 4 + length, cumulative)
    (rich_cache / 'book.bin').write_bytes(metadata)
    checks = []
    expected = json.loads((ROOT / 'test/reader_lifecycle/section_driver_hashes.json').read_text())
    commands = [('compile', section_command(ROOT, output, cpp, binary, os.environ.get('XP_TEST_RUST_ARCHIVE')))]
    cases = {}
    for case, case_book, case_cache in [('plain', book, cache), ('rich', rich_book, rich_cache)]:
        for mode in ('complete', 'partial'):
            for popup in ('none', 'popup'):
                name = case + '-' + mode + '-' + popup
                commands.append((name, [str(binary), str(case_book), str(case_cache), mode, popup, str(10 if case == 'plain' else 1112)]))
                cases[name] = (case_cache, expected[case + '-' + mode])
    for name, command in commands:
        result = subprocess.run(command, capture_output=True)
        (output / (name + '.log')).write_bytes(result.stdout + result.stderr)
        check = dict(name=name, command=command, returncode=result.returncode)
        checks.append(check)
        (output / 'results.json').write_text(json.dumps(dict(checks=checks, source_sha256=hashlib.sha256(source.encode()).hexdigest(),
            production=['ReaderBook', 'ReaderSection', 'BuildOwner', 'BuildServices', 'Chapter',
                        'Page via page_load', 'cache-only Section', 'ProgressMapper', 'native physical file/resource leaves'],
            policy_hooks=0, legacy_fixture_owner=False,
            limits=['host storage and font fakes', 'not a target timing or heap measurement']), indent=2) + '\n')
        if result.returncode:
            raise RuntimeError((result.stdout + result.stderr).decode(errors='replace')[-6000:])
        if name in cases:
            case_cache, frozen = cases[name]
            data = (case_cache / 'sections/0.bin').read_bytes()
            actual = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
            check['cache'] = actual
            assert actual == frozen, (name, actual, frozen)
    # The retired native null-popup mutation remains in the historical oracle.
    # This mutant checks the corresponding callback behavior on the actual owner.
    production_path = ROOT / 'rust/crates/xp-ffi/src/app/epub/section_owner/lifecycle.rs'
    production = production_path.read_bytes()
    copied_repo = output / 'popup-mutant-source'
    copied_rust = copied_repo / 'rust'
    shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
    for dependency in ('scripts', 'lib', 'src'):
        (copied_repo / dependency).symlink_to(ROOT / dependency, target_is_directory=True)
    anchor = b'            popup,\n            css: None,'
    assert production.count(anchor) == 1
    changed = production.replace(anchor, b'            popup: None,\n            css: None,', 1)
    (copied_repo / production_path.relative_to(ROOT)).write_bytes(changed)
    target = output / 'popup-mutant-target'
    mutant_binary = output / 'popup-mutant'
    commands = [
        ('popup-mutant-build', ['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
          '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
          '--features', 'app-test,chapter-test,chapter-trace,section-test']),
        ('popup-mutant-link', section_command(ROOT, output, cpp, mutant_binary, str(target / 'release/libxp_ffi.a'))),
        ('popup-mutant-none', [str(mutant_binary), str(book), str(cache), 'complete', 'none', '10']),
        ('popup-mutant-some', [str(mutant_binary), str(book), str(cache), 'complete', 'popup', '10']),
    ]
    try:
        for name, command in commands:
            result = subprocess.run(command, capture_output=True)
            (output / (name + '.log')).write_bytes(result.stdout + result.stderr)
            checks.append(dict(name=name, command=command, returncode=result.returncode))
            if name == 'popup-mutant-some':
                assert result.returncode != 0, 'production omit-popup mutant survived'
            else:
                assert result.returncode == 0, (name, result.stderr.decode(errors='replace')[-6000:])
    finally:
        assert production_path.read_bytes() == production, 'real production source changed'
        (output / 'popup-mutation.json').write_text(json.dumps(dict(
            checks=checks[-4:], source=str(production_path.relative_to(ROOT)),
            source_sha256=hashlib.sha256(production).hexdigest(),
            mutant_sha256=hashlib.sha256(changed).hexdigest(), replacement_count=1,
            historical_mutation='reader_book_render: native-null-popup',
            approval='msg_1950a926bbb2'), indent=2) + '\n')
    result_path = output / 'results.json'
    summary = json.loads(result_path.read_text())
    summary['checks'] = checks
    result_path.write_text(json.dumps(summary, indent=2) + '\n')
    for name in cases:
        print(name + ': ' + '\n'.join(line for line in (output / (name + '.log')).read_text().splitlines() if line.startswith('Section driver:')))


if __name__ == '__main__':
    main()
