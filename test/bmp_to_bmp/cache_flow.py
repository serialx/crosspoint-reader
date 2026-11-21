"""Exercise the sleep activity's threshold choice and cache flow with a recording display and card."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/activities/boot_sleep/SleepActivity.cpp').read_text()


def definition(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    functions = ['HalDisplay::GrayscaleMode sleepGrayscaleMode(', 'bool useOriginalCoverThresholds(',
                 'bool SleepActivity::renderCustomSleepBmp(']
    (work / 'sleep.inc').write_text('\n'.join(map(definition, functions)))
    binary = work / 'cache-flow'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-UNDEBUG', '-I', str(work),
                    '-I', str(ROOT / 'lib/Memory'), '-I', str(ROOT / 'src/activities/boot_sleep'),
                    str(Path(__file__).with_name('cache_flow.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
