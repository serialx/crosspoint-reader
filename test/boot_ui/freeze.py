"""Record screen policy directly from the pre-port C++ source tree."""
import pathlib
import sys
ROOT = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2]
METHODS = {
    'boot_sleep/BootActivity.cpp': ['void BootActivity::onEnter()'],
    'boot_sleep/SleepActivity.cpp': ['BitmapPlacement calculateBitmapPlacement(', 'void SleepActivity::onEnter()', 'void SleepActivity::renderDefaultSleepScreen() const', 'void SleepActivity::renderLastScreenSleepScreen() const', 'void SleepActivity::renderBlankSleepScreen() const'],
    'home/CrashActivity.cpp': ['void CrashActivity::loop()', 'void CrashActivity::render(RenderLock&&)'],
    'reader/XtcReaderActivity.cpp': ['bool XtcReaderActivity::pageTurn(bool isForward)', 'bool XtcReaderActivity::skipPages(int amount)', 'bool XtcReaderActivity::isAtEndOfBook() const', 'void XtcReaderActivity::onReturnFromEndOfBook()'],
    'util/BmpViewerActivity.cpp': ['void BmpViewerActivity::loop()'],
}
out = ['// Frozen from 31d5cd46 by freeze.py, before the Rust screen port.\n']
for path, names in METHODS.items():
    source = (ROOT / 'src/activities' / path).read_text()
    for name in names:
        start = source.index(name)
        pos = source.index('{', start)
        depth = 1
        end = pos + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        out.append(source[start:end] + '\n')
pathlib.Path(__file__).with_name('baseline.inc').write_text('\n'.join(out))
