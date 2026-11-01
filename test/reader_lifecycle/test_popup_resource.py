"""Exercise actual OptionPopup and native popup leaves with physical SDK fakes.

The complete widget class is extracted unchanged except private access for test
observations. No selection/render-cap policy is reproduced in the fixture.
Extraction is deliberately anchored and fails on drift; it supports ordinary C++
strings/comments, not raw string literals. Mutants edit isolated production-source
copies and must compile before a specific runtime assertion rejects them.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
POPUP = Path('src/components/OptionPopup.h')
NATIVE = Path('src/activities/reader/RustEpubResources.cpp')


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError(f'Expected one source anchor, found {text.count(before)}: {before}')
    return text.replace(before, after, 1)


def closing_brace(text, opening):
    if text[opening] != '{':
        raise ValueError('Expected opening brace')
    depth = 0
    index = opening
    while index < len(text):
        if text.startswith('//', index):
            end = text.find('\n', index + 2)
            index = len(text) if end < 0 else end + 1
            continue
        if text.startswith('/*', index):
            end = text.find('*/', index + 2)
            if end < 0:
                raise ValueError('Unterminated source comment')
            index = end + 2
            continue
        if text.startswith('R"', index):
            raise ValueError('Raw string introduced: update fragment scanner explicitly')
        if text[index] in ('"', "'"):
            quote = text[index]
            index += 1
            while index < len(text):
                if text[index] == '\\':
                    index += 2
                    continue
                if text[index] == quote:
                    index += 1
                    break
                index += 1
            else:
                raise ValueError('Unterminated source string')
            continue
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    raise ValueError('Unterminated source body')


def definition(text, signature):
    if text.count(signature) != 1:
        raise ValueError('Source definition anchor changed: ' + signature)
    start = text.index(signature)
    opening = text.index('{', start)
    return text[start:closing_brace(text, opening)]


def popup_class(text):
    result = definition(text, 'class OptionPopup {') + ';'
    return replace_once(result, '\n private:\n', '\n public:\n')


def native_leaf(text, name):
    matches = list(re.finditer(r'\.' + re.escape(name) + r'\s*=\s*', text))
    if len(matches) != 1:
        raise ValueError('Expected one native initializer: ' + name)
    start = matches[0].end()
    if not text.startswith('[]', start):
        raise ValueError('Native leaf is no longer a captureless lambda: ' + name)
    end = closing_brace(text, text.index('{', start))
    if not text[end:].lstrip().startswith(','):
        raise ValueError('Native leaf expression boundary changed: ' + name)
    return text[start:end]


def fixture(popup, native, template):
    result = replace_once(template, '// ACTUAL_OPTION_POPUP', popup_class(popup))
    result = replace_once(result, '// ACTUAL_PATH_TEXT', definition(native, 'const char* pathText(XpAppText value)'))
    for marker, name in [('SHOW', 'popup_show'), ('INPUT', 'popup_input'),
                         ('ACTIVE', 'popup_active'), ('DISMISS', 'popup_dismiss')]:
        result = replace_once(result, '/* ACTUAL_' + marker + '_LEAF */', native_leaf(native, name))
    return result


def run_case(output, name, popup, native, template, compiler, failure=None):
    directory = output / name
    directory.mkdir()
    # Keep exact isolated production copies beside each compiled extraction.
    (directory / 'OptionPopup.h').write_text(popup)
    (directory / 'RustEpubResources.cpp').write_text(native)
    source = directory / 'oracle.cpp'
    source.write_text(fixture(popup, native, template))
    binary = directory / 'oracle'
    command = [compiler, '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-I', str(ROOT / 'rust/crates/xp-ffi/include'), str(source), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, text=True)
    (directory / 'compile.log').write_text(built.stdout + built.stderr)
    if built.returncode:
        raise RuntimeError(f'{name} did not compile; a compile failure is not a killed mutant:\n{built.stderr}')
    ran = subprocess.run([str(binary)], capture_output=True, text=True)
    log = ran.stdout + ran.stderr
    (directory / 'run.log').write_text(log)
    if failure is None:
        if ran.returncode:
            raise RuntimeError(f'Baseline popup resource assertions failed:\n{log}')
    elif ran.returncode == 0 or failure not in log:
        raise RuntimeError(f'{name} was not rejected by its intended runtime assertion ({failure}):\n{log}')
    return {'name': name, 'compile_argv': command, 'compile_returncode': built.returncode,
            'run_returncode': ran.returncode, 'expected_assertion': failure,
            'popup_sha256': hashlib.sha256(popup.encode()).hexdigest(),
            'native_sha256': hashlib.sha256(native.encode()).hexdigest()}


def exercise(output, compiler):
    popup = (ROOT / POPUP).read_text()
    native = (ROOT / NATIVE).read_text()
    template = (HERE / 'popup_resource_oracle.cpp').read_text()
    results = []
    manifest = {'source': {str(POPUP): hashlib.sha256(popup.encode()).hexdigest(),
                           str(NATIVE): hashlib.sha256(native.encode()).hexdigest(),
                           'popup_resource_oracle.cpp': hashlib.sha256(template.encode()).hexdigest()},
                'tests': results,
                'scope': 'actual widget and native leaf contract; deterministic physical SDK fakes; no golden'}
    def execute(name, widget, resource, failure=None):
        results.append(run_case(output, name, widget, resource, template, compiler, failure))
        (output / 'results.json').write_text(json.dumps(manifest, indent=2) + '\n')
    execute('baseline', popup, native)
    # Alter the actual button-confirm ordering, not the test's expected event list.
    old = '      if (onSelectCallback) onSelectCallback(selectedIndex);\n      requestUpdate();'
    reordered = replace_once(popup, old, '      requestUpdate();\n      if (onSelectCallback) onSelectCallback(selectedIndex);')
    execute('choice-after-update', reordered, native, 'choice must precede requestUpdate')
    # Native truncation would lose labels beyond the widget's displayed cap.
    clipped = replace_once(native, 'pathText(title), pathText(message), static_cast<int>(count), selected,',
                           'pathText(title), pathText(message), std::min<int>(count, 16), selected,')
    execute('native-label-count-clipped', popup, clipped, 'popup.ownedStrings == expected')
    # Keep callback behavior but make its production capture too large for SBO.
    allocated = replace_once(native,
                             '[choose, choiceContext](int index) { choose(choiceContext, index); }',
                             '[choose, choiceContext, padding = std::array<void*, 8>{}](int index) { '
                             '(void)padding; choose(choiceContext, index); }')
    execute('retained-choice-allocates', popup, allocated, 'two-pointer choice assignment must stay inline')
    print(json.dumps(manifest), flush=True)
    print('popup resource: synchronous owned labels, 33 labels/16 displayed, empty and 255-byte labels, '
          'source lifetime, button/touch/cancel ordering, legacy overloads, zero callback assignment '
          'allocations after preallocation; three actual-source mutants rejected')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--output', type=Path, help='optional new directory retaining source copies and logs')
    args = parser.parse_args()
    if args.output:
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=False)
        exercise(output, args.cxx)
    else:
        with tempfile.TemporaryDirectory(prefix='reader-popup-resource-') as directory:
            exercise(Path(directory), args.cxx)


if __name__ == '__main__':
    main()
