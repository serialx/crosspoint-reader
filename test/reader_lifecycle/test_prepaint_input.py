"""Freeze the physical-button window after end-book load and before first paint."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
sys.path.insert(0, str(HERE))
from extract import definition as method

base = (ROOT / 'src/activities/reader/ReaderActivity.cpp').read_text()
end = (ROOT / 'src/activities/reader/EndOfBookOptions.cpp').read_text()
methods = '\n'.join([method(base, 'void ReaderActivity::render('),
                      method(base, 'bool ReaderActivity::endOfBookMenuActive('),
                      method(end, 'EndOfBookOptions::Action EndOfBookOptions::handleMenuInput(')])
fixture = (HERE / 'prepaint_input.cpp').read_text().replace('// PREPAINT_METHODS', methods)
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / 'prepaint.cpp').write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', str(p / 'prepaint.cpp'),
                    *rust_link_flags(), '-o', str(p / 'prepaint')], check=True)
    subprocess.run([str(p / 'prepaint')], check=True)
    # Moving readiness past the first paint loses physical-button ownership.
    mutated = fixture.replace('endOfBookOptionsReady.store(true, std::memory_order_release);', '')
    (p / 'delayed.cpp').write_text(mutated)
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', str(p / 'delayed.cpp'),
                    *rust_link_flags(), '-o', str(p / 'delayed')], check=True)
    failed = subprocess.run([str(p / 'delayed')], capture_output=True, text=True)
    assert failed.returncode != 0
    print('prepaint: physical Back/Confirm/next/previous accepted; touch and remote refused; delayed readiness rejected')
