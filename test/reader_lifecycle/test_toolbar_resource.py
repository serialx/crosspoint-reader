"""Exercise actual toolbar methods/native leaves with physical SDK fakes.

The Rust toolbar view/navigation remain linked production code. The fixture
extracts complete native class declarations and method bodies, changing only
private access for observations. Once-per-row applies to each resources.window
request, and to the preallocated-command paint; existing capacity retries are
preserved. Allocation assertions describe this host, not ESP32-C3 string SBO.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from test_popup_resource import definition, native_leaf, replace_once

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

TOOLBAR_HEADER = Path('src/activities/reader/ReaderToolbarUi.h')
TOOLBAR_SOURCE = Path('src/activities/reader/ReaderToolbarUi.cpp')
COMMAND_HEADER = Path('src/activities/reader/ReaderChromeListActivity.h')
COMMAND_SOURCE = Path('src/activities/reader/ReaderChromeListActivity.cpp')
NATIVE = Path('src/activities/reader/RustEpubResources.cpp')
MEMORY = Path('lib/Memory/Memory.h')
PATHS = (TOOLBAR_HEADER, TOOLBAR_SOURCE, COMMAND_HEADER, COMMAND_SOURCE, NATIVE, MEMORY)
METHODS = (
    'ReaderToolbarUi::ReaderToolbarUi(GfxRenderer& renderer)',
    'void ReaderToolbarUi::begin()',
    'void ReaderToolbarUi::resetNavigation(',
    'bool ReaderToolbarUi::scrollNavigation(',
    'int ReaderToolbarUi::visibleRows() const',
    'ReaderToolbarUi::Routed ReaderToolbarUi::route(',
    'const char* ReaderToolbarUi::text(',
    'XpUiReaderRows ReaderToolbarUi::resources()',
    'bool ReaderToolbarUi::render()',
)
LEAVES = (('OPEN', 'toolbar_open'), ('CLOSE', 'toolbar_close'),
          ('BEGIN', 'toolbar_begin'), ('PAINT', 'toolbar_paint'),
          ('ROUTE', 'toolbar_route'), ('SCROLL', 'toolbar_scroll'),
          ('VISIBLE', 'toolbar_visible_rows'), ('RESET', 'toolbar_reset_navigation'))


def digest(value):
    return hashlib.sha256(value.encode()).hexdigest()


def fixture(sources, template):
    command_class = definition(sources[COMMAND_HEADER], 'class ReaderChromeCommands {') + ';'
    command_methods = '\n\n'.join(definition(sources[COMMAND_SOURCE], signature) for signature in
                                   ('bool ReaderChromeCommands::reserve(', 'bool ReaderChromeCommands::grow()'))
    toolbar_class = definition(sources[TOOLBAR_HEADER], 'class ReaderToolbarUi {') + ';'
    toolbar_class = replace_once(toolbar_class, '\n private:\n', '\n public:\n')
    methods = '\n\n'.join(definition(sources[TOOLBAR_SOURCE], signature) for signature in METHODS)
    replacements = {
        '// ACTUAL_COMMAND_CLASS': command_class,
        '// ACTUAL_COMMAND_METHODS': command_methods,
        '// ACTUAL_TOOLBAR_CLASS': toolbar_class,
        '// ACTUAL_TOOLBAR_METHODS': methods,
        '// ACTUAL_PATH_TEXT': definition(sources[NATIVE], 'const char* pathText(XpAppText value)'),
    }
    for marker, value in replacements.items():
        template = replace_once(template, marker, value)
    for marker, name in LEAVES:
        template = replace_once(template, '/* ACTUAL_' + marker + '_LEAF */', native_leaf(sources[NATIVE], name))
    return template


def run_case(output, name, sources, template, compiler, link_flags, failure=None):
    directory = output / name
    directory.mkdir()
    for path, source in sources.items():
        (directory / path.name).write_text(source)
    source = directory / 'oracle.cpp'
    source.write_text(fixture(sources, template))
    binary = directory / 'oracle'
    command = [compiler, '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-I', str(directory), str(source), *link_flags, '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, text=True)
    (directory / 'compile.log').write_text(built.stdout + built.stderr)
    if built.returncode:
        raise RuntimeError(f'{name} did not compile; compile failure does not kill a mutant:\n{built.stderr}')
    ran = subprocess.run([str(binary)], capture_output=True, text=True)
    log = ran.stdout + ran.stderr
    (directory / 'run.log').write_text(log)
    if failure is None:
        if ran.returncode:
            raise RuntimeError(f'Baseline toolbar resource assertions failed:\n{log}')
    elif ran.returncode == 0 or failure not in log:
        raise RuntimeError(f'{name} missed its required runtime assertion ({failure}):\n{log}')
    return {'name': name, 'compile_argv': command, 'compile_returncode': built.returncode,
            'run_argv': [str(binary)], 'run_returncode': ran.returncode, 'expected_assertion': failure,
            'extraction_sha256': digest(source.read_text()),
            'source_sha256': {str(path): digest(text) for path, text in sources.items()}}


def exercise(output, compiler, archive):
    sources = {path: (ROOT / path).read_text() for path in PATHS}
    template = (HERE / 'toolbar_resource_oracle.cpp').read_text()
    link_flags = rust_link_flags(archive=archive)
    archive_path = Path(link_flags[-1]).resolve()
    manifest = {
        'test': 'reader_toolbar_resource',
        'scope': 'actual native toolbar class/render and leaves; production Rust view/nav/route; physical SDK fakes',
        'once_per_row': 'per resources.window request; per paint only with preallocated commands; retries unchanged',
        'allocation_scope': 'preallocated command/window paint on this host; not ESP32-C3 SBO proof',
        'source_sha256': {str(path): digest(source) for path, source in sources.items()},
        'fixture_sha256': digest(template),
        'driver_sha256': digest(Path(__file__).read_text()),
        'scanner_sha256': digest((HERE / 'test_popup_resource.py').read_text()),
        'rust_archive': str(archive_path),
        'rust_archive_sha256': hashlib.sha256(archive_path.read_bytes()).hexdigest(),
        'tests': [],
    }

    def execute(name, inputs, failure=None):
        manifest['tests'].append(run_case(output, name, inputs, template, compiler, link_flags, failure))
        (output / 'results.json').write_text(json.dumps(manifest, indent=2) + '\n')

    execute('baseline', sources)
    duplicate = dict(sources)
    old = ('self.windowValues_[i], self.rowFlags[i])) {\n'
           '            return false;\n          }\n')
    new = old + ('          self.model_.rowFill(self.model_.rowFillContext, index, self.windowLabels_[i],\n'
                 '                              self.windowValues_[i], self.rowFlags[i]);\n')
    duplicate[TOOLBAR_SOURCE] = replace_once(sources[TOOLBAR_SOURCE], old, new)
    execute('duplicate-row-fill', duplicate, 'rowFill at most once in visible order')
    retained = dict(sources)
    retained[NATIVE] = replace_once(sources[NATIVE],
                                   '        ScopedCleanup clearModel{[&toolbar] { toolbar.setModel({}); }};',
                                   '        // Mutant retains the stack-backed row source in Model.')
    execute('retained-model', retained, 'borrowed Model must be cleared after paint')
    ignored = dict(sources)
    ignored[NATIVE] = replace_once(sources[NATIVE],
        '          return source.rows && source.rows(source.context, index, copy, &output) && output.copied;',
        '          if (source.rows) source.rows(source.context, index, copy, &output);\n'
        '          return output.copied;')
    execute('ignored-row-source-failure', ignored, 'row-source failure must reject paint')
    indentation = dict(sources)
    indentation[NATIVE] = replace_once(sources[NATIVE],
                                      "out.label.assign(row->label_spaces, ' ');",
                                      "out.label.clear();")
    execute('ignored-indentation', indentation, 'temporary source bytes must be copied before painting')
    print(json.dumps(manifest), flush=True)
    print('toolbar resource: whole production class/render/native leaves; real Rust view/navigation; '
          'ordered direct window fill; synchronous borrowed source copies; cleared Model success/failure; '
          'missing row source rejected; route/scroll/reset forwarding; zero preallocated host paint allocations; '
          'four actual-source mutants rejected')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--archive', help='existing host Rust archive; otherwise use rust_host environment/default')
    parser.add_argument('--output', type=Path, help='new directory retaining isolated source copies and logs')
    args = parser.parse_args()
    if args.output:
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=False)
        exercise(output, args.cxx, args.archive)
    else:
        with tempfile.TemporaryDirectory(prefix='reader-toolbar-resource-') as directory:
            exercise(Path(directory), args.cxx, args.archive)


if __name__ == '__main__':
    main()
