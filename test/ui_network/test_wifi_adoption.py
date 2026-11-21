"""Exercise native Wi-Fi identity through the shipping Rust App adoption path."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

manager = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
facts = manager.split('.facts = ', 1)[1].split(',\n      .with_result', 1)[0]
ownership = (ROOT / 'src/activities/OwnedLegacyActivity.h').read_text()
ownership = ownership[ownership.index('struct OwnedLegacyActivity {'):]
header = (ROOT / 'rust/crates/xp-ffi/include/xp_network_owner.h').read_text()
physical = header.split('} XpWifiCredentialView;', 1)[1].split('} XpWifiPhysical;', 1)[0]
fields = re.findall(r'\(\*(\w+)\)', physical)
assert len(fields) == 39
source = Path(__file__).with_name('wifi_adoption.cpp.in').read_text()
source = source.replace('@OWNERSHIP@', ownership).replace('@FACTS@', facts)
source = source.replace('@DEFAULTS@', '\n'.join(
    f'  value.{name} = &Unexpected<decltype(value.{name})>::call;' for name in fields))
with tempfile.TemporaryDirectory(prefix='wifi-app-adoption-') as tmp:
    directory = Path(tmp)
    cpp = directory / 'adoption.cpp'
    binary = directory / 'adoption'
    cpp.write_text(source)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Werror', '-UNDEBUG',
                    '-I', str(ROOT / 'test/ui_runtime'), str(cpp),
                    *rust_link_flags(sys.argv[1]), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
