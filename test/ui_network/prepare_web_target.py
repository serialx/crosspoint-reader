from pathlib import Path
import re
import sys
import prepare
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
source = (HERE / 'WebTarget.cpp').read_text()
original = (HERE / 'web_render_original.inc').read_text().replace('CrossPointWebServerActivity::', 'Original::')
production = (ROOT / 'src/activities/network/CrossPointWebServerActivity.cpp').read_text()
methods = '\n'.join(prepare.extract(production, 'CrossPointWebServerActivity', name) for name in ['buildRustScene', 'rustResources'])
qr = re.sub(r'^#include[^\n]*\n', '', (ROOT / 'src/util/QrUtils.cpp').read_text(), flags=re.M)
source = source.replace('// ORIGINAL_RENDER', original).replace('// PRODUCTION_METHODS', methods).replace('// QR_LEAF', qr)
Path(sys.argv[1]).write_text(source)

header = re.sub(r'^#(?:include|pragma)[^\n]*\n', '', (ROOT/'src/activities/network/NetworkUiBinding.h').read_text(), flags=re.M)
binding = re.sub(r'^#include[^\n]*\n', '', (ROOT/'src/activities/network/NetworkUiBinding.cpp').read_text(), flags=re.M)
source = source.replace("// SHARED_BINDING", header+"\n"+binding)
Path(sys.argv[1]).write_text(source)
