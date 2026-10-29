"""Compile production network lifecycle methods against deterministic host leaves."""
from pathlib import Path
import re
import sys
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
METHODS = {
    'UsbDriveActivity': ['onEnter', 'onExit', 'loop', 'restartToHome'],
    'WifiSelectionActivity': ['startWifiScan', 'processWifiScanResults', 'appendHiddenNetworkEntry',
        'selectNetwork', 'hasAttemptedAutoSsid', 'tryAutoConnectCredential',
        'tryNextSavedNetworkFromScan', 'handleAutoConnectFailure',
        'showNetworkListFromAutoConnect', 'attemptConnection', 'checkConnectionStatus', 'loop', 'onComplete'],
    'CalibreConnectActivity': ['onExit', 'onWifiSelectionComplete', 'startWebServer', 'loop'],
    'CrossPointWebServerActivity': ['onExit', 'startAccessPoint', 'startWebServer', 'loop'],
}
def extract(source, cls, name):
    match = re.search(r'^.*\b'+cls+'::'+name+r'\(', source, re.M)
    if not match:
        raise ValueError(f'Missing {cls}::{name}')
    start = match.start()
    begin = source.index('{', match.end())
    level, i = 1, begin+1
    while level:
        level += (source[i] == '{') - (source[i] == '}')
        i += 1
    return source[start:i]
def helper(source, name):
    match = re.search(r'^(?:void|int) '+name+r'\(', source, re.M)
    start = match.start()
    begin = source.index('{', match.end())
    level, i = 1, begin+1
    while level:
        level += (source[i] == '{') - (source[i] == '}')
        i += 1
    return source[start:i]
def generate(root=ROOT):
    parts = []
    for cls, methods in METHODS.items():
        source = (root/'src/activities/network'/f'{cls}.cpp').read_text()
        parts += [extract(source, cls, m) for m in methods]
    source = (HERE/'effects.cpp').read_text()
    source = source.replace('// PRODUCTION_METHODS', '\n\n'.join(parts))
    original = (root/'src/activities/network/CrossPointWebServerActivity.cpp').read_text()
    return source.replace('// PRODUCTION_HELPERS', '\n\n'.join(helper(original, name) for name in ['stopDnsServer', 'restartMdns', 'barsForRssi']))
if __name__ == '__main__':
    Path(sys.argv[1]).write_text(generate(Path(sys.argv[2]) if len(sys.argv)>2 else ROOT))
