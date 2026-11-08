"""Real native store/leaves and App owner, with placement-address and ASan guards."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--archive', required=True)
p.add_argument('--arduinojson', type=Path, required=True)
p.add_argument('--mbedtls', type=Path, required=True)
a = p.parse_args()
manager = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
facts = manager.split('.facts = ', 1)[1].split(',\n      .with_result', 1)[0]
ownership = (ROOT / 'src/activities/OwnedLegacyActivity.h').read_text()
ownership = ownership[ownership.index('struct OwnedLegacyActivity {'):]
header = (ROOT / 'rust/crates/xp-ffi/include/xp_network_owner.h').read_text()
physical = header.split('} XpWifiCredentialView;', 1)[1].split('} XpWifiPhysical;', 1)[0]
fields = re.findall(r'\(\*(\w+)\)', physical)
# Split the actual immutable native table initializer at its top-level commas.
native = (ROOT / 'src/activities/network/WifiSelectionPhysical.cpp').read_text()
table = native.split('static constexpr XpWifiPhysical table() {', 1)[1].split('return {', 1)[1]
parts, begin, depth = [], 0, 0
for i, char in enumerate(table):
    if char in '({[': depth += 1
    elif char in ')}]':
        if char == '}' and depth == 0:
            parts.append(table[begin:i].strip()); break
        depth -= 1
    elif char == ',' and depth == 0:
        parts.append(table[begin:i].strip()); begin = i + 1
assert len(parts) == len(fields) == 39
leaves = dict(zip(fields, parts))
selected = ['last_ssid', 'last_ssid_drop', 'credential_lookup', 'credential_drop',
            'store_load', 'store_count', 'store_has', 'store_remove']
functions = '\n'.join(f'const auto native_{name} = {leaves[name]};' for name in selected)
addon = Path(__file__).with_name('wifi_saved_loans.cpp.in').read_text()
prelude, main = addon.split('// MAIN\n')
prelude = prelude.replace('@NATIVE_FUNCTIONS@', functions)
source = Path(__file__).with_name('wifi_adoption.cpp.in').read_text().split('int main() {', 1)[0]
source = source.replace('extern "C" const XpWifiPhysical', prelude + '\nextern "C" const XpWifiPhysical')
assignments = r'''
  value.allow_auto = [](void*) -> uint8_t { return true; };
  value.last_ssid = [](XpWifiLastSsidStorage* p) { assert(!lastSlot); lastSlot = p; return native_last_ssid(p); };
  value.last_ssid_drop = [](XpWifiLastSsidStorage* p) {
    assert(lastSlot == p && "native last-SSID storage moved"); lastSlot = nullptr; native_last_ssid_drop(p);
  };
  value.credential_lookup = [](XpWifiCredentialStorage* p, XpAppText key, XpWifiCredentialView* out) -> uint8_t {
    assert(!credentialSlot); credentialSlot = p; return native_credential_lookup(p, key, out);
  };
  value.credential_drop = [](XpWifiCredentialStorage* p) {
    assert(credentialSlot == p && "native credential storage moved"); credentialSlot = nullptr; native_credential_drop(p);
  };
  value.store_load = native_store_load; value.store_count = native_store_count;
  value.store_has = native_store_has; value.store_remove = native_store_remove;
  value.connect = [](const char* ssid, const char* password, uint8_t usePassword) {
    assert(savedName == ssid && usePassword && savedPassword == password); ++connects;
  };
  value.connection_status = []() -> int32_t { return connection; };
  value.diagnostic = [](XpWifiDiagnostic event, XpAppText, uint32_t) { hiddenSaved += event == XP_WIFI_LOG_HIDDEN_SAVED; };
'''
source = source.replace('  return value;\n}();', assignments + '\n  return value;\n}();') + main
source = source.replace('@OWNERSHIP@', ownership).replace('@FACTS@', facts)
source = source.replace('#define LOG_ERR(...) ((void)0)', '#include <Logging.h>')
source = source.replace('@DEFAULTS@', '\n'.join(
    f'  value.{name} = &Unexpected<decltype(value.{name})>::call;' for name in fields))
with tempfile.TemporaryDirectory(prefix='wifi-saved-native-') as tmp:
    d = Path(tmp); cpp = d / 'saved.cpp'; binary = d / 'saved'; cpp.write_text(source)
    includes = [ROOT / 'test/settings_store/stubs', ROOT / 'src', ROOT / 'lib/Serialization',
                a.arduinojson, a.mbedtls / 'include', a.mbedtls / 'library', ROOT / 'test/ui_runtime']
    flags = [f'-I{x}' for x in includes]
    subprocess.run(['cc', '-c', str(a.mbedtls / 'library/base64.c'), *flags,
                    '-DMBEDTLS_CONFIG_FILE="oracle_mbedtls.h"', '-o', str(d / 'base64.o')], check=True)
    subprocess.run(['c++', '-std=c++20', '-O1', '-g', '-fsanitize=address', '-fno-omit-frame-pointer',
                    '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0', '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0',
                    '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0', *flags, *rust_link_flags(a.archive),
                    str(ROOT / 'src/WifiCredentialStore.cpp'),
                    str(ROOT / 'lib/Serialization/PersistableStore.cpp'),
                    str(ROOT / 'lib/Serialization/ObfuscationUtils.cpp'),
                    str(d / 'base64.o'), str(cpp), '-o', str(binary)], check=True)
    for ssid, password in [('1', 'pw'), ('fifteen-letters!', 'p' * 15), ('long-saved-network-name', 'p' * 64)]:
        subprocess.run([str(binary), ssid, password], check=True)
