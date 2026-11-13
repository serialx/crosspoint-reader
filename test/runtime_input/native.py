"""Exercise the production native input phases and explicit-Home query overloads."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def methods(path, owner, names):
    source = path.read_text()
    pattern = rf'^(?:XpHalInputPoll|void|bool|unsigned long|HomeButtonAction) {owner}::(?:{"|".join(names)})\('
    found = []
    for match in re.finditer(pattern, source, re.M):
        opening = source.index('{', match.start())
        depth, end = 1, opening + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        found.append(source[match.start():end])
    return '\n'.join(found)


with tempfile.TemporaryDirectory(prefix='runtime-input-') as directory:
    out = Path(directory)
    (out / 'HalGPIO.h').write_text((HERE / 'gpio.h').read_text())
    code = (HERE / 'native.cpp').read_text()
    code = code.replace('// PRODUCTION', methods(ROOT / 'lib/hal/HalGPIO.cpp', 'HalGPIO', ['update', 'pollRaw']) + '\n' +
                        methods(ROOT / 'src/MappedInputManager.cpp', 'MappedInputManager',
                                ['update', 'pollRaw', 'pollFeedback', 'pollFinish', 'homeButtonAction',
                                 'wasPressed', 'wasReleased', 'wasLongPressed', 'longPressReady', 'finishLongPress',
                                 'suppressNextRelease', 'consumeSuppressedRelease', 'isPressed', 'getHeldTime',
                                 'wasHomeGesture', 'injectRemoteClick']))
    reader = (ROOT / 'src/activities/reader/ReaderUtils.h').read_text()
    sources = reader[reader.index('template <bool Raw>'):reader.index('inline bool gestureAllowsSwipe')]
    code = code.replace('// READER SOURCES', '\nnamespace ReaderUtils {\n' + sources + '\n}')
    (out / 'check.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-DFREEINK_CAP_TOUCH=0', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(out), '-I' + str(ROOT / 'src'),
                    '-I' + str(ROOT / 'rust/crates/xp-ffi/include'), str(out / 'check.cpp'),
                    '-o', str(out / 'check')], check=True)
    subprocess.run([str(out / 'check')], check=True)
