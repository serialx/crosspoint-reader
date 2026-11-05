"""Record the original activity control flow with deterministic platform leaves."""
from pathlib import Path
import subprocess
import tempfile
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def record(mutate=False):
    methods = (HERE / 'original.inc').read_text()
    if mutate:
        methods = methods.replace('selectedOption == 0', 'selectedOption == 1')
    source = (HERE / 'effects.cpp').read_text().replace('// METHODS', methods)
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / 'effects.cpp'
        exe = Path(directory) / 'effects'
        cpp.write_text(source)
        subprocess.run(['c++', '-std=c++20', '-DCONFIG_IDF_TARGET_ESP32C3=1',
                        '-I'+str(ROOT/'lib/KOReaderSync'), str(cpp), '-o', str(exe)], check=True)
        return subprocess.check_output([str(exe)])

if __name__ == '__main__':
    result = record()
    if '--freeze' in sys.argv:
        (HERE / 'effects.golden').write_bytes(result)
    else:
        assert result == (HERE / 'effects.golden').read_bytes()
        assert record(True) != result, 'choice mutation escaped the effects oracle'
        print('Original sync/auth effects and deliberate choice mutation: passed')
