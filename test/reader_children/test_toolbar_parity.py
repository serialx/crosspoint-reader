"""Compare the actual native Toolbar with the owned Rust drawing adapter."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'test/reader_lifecycle'))
from test_toolbar_resource import fixture, PATHS
from test_popup_resource import replace_once
from rust_host import rust_link_flags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True)
    args = parser.parse_args()
    template = (ROOT / 'test/reader_lifecycle/toolbar_resource_oracle.cpp').read_text()
    template = template.split('int main(', 1)[0]
    template = replace_once(template, '#include <string>', '#include <string>\n#include <vector>\n#include "app_leaves.h"\n#include <xp_ui_utility.h>')
    template = replace_once(template, 'class GfxRenderer {', '''
static std::vector<XpUiCommand> recordedCommands;
static std::vector<std::string> recordedTexts;
class GfxRenderer {''')
    template = replace_once(template, '    ++texts;', '    ++texts;\n    recordedTexts.emplace_back(text);')
    template = replace_once(template, '    ++scenes;', '    ++scenes;\n    recordedCommands.assign(scene.commands, scene.commands + scene.command_count);')
    source = fixture({path: (ROOT / path).read_text() for path in PATHS}, template)
    source += (HERE / 'toolbar_parity.cpp.in').read_text()
    with tempfile.TemporaryDirectory(prefix='reader-toolbar-parity-') as temporary:
        out = Path(temporary)
        candidate = out / 'candidate.cpp'
        candidate.write_text(source)
        binary = out / 'candidate'
        subprocess.run(['c++', '-std=c++20', '-O0', '-g', '-UNDEBUG',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-I', str(ROOT / 'test/ui_runtime'), '-I', str(ROOT / 'lib/Memory'),
                        str(candidate), *rust_link_flags(archive=args.archive), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
