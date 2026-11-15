from pathlib import Path
import sys
import re
import prepare
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
source=(HERE/'CalibreTarget.cpp').read_text()
original=(HERE/'calibre_render_original.inc').read_text().replace('CalibreConnectActivity::render','Original::render')
production=(ROOT/'src/activities/network/CalibreConnectActivity.cpp').read_text()
methods='\n'.join(prepare.extract(production,'CalibreConnectActivity',m) for m in ['text','buildRustScene','rustResources'])
source=source.replace('// ORIGINAL_RENDER',original).replace('// PRODUCTION_METHODS',methods).replace('millis()', 'testNow')
Path(sys.argv[1]).write_text(source)

header = re.sub(r'^#(?:include|pragma)[^\n]*\n', '', (ROOT/'src/activities/network/NetworkUiBinding.h').read_text(), flags=re.M)
binding = re.sub(r'^#include[^\n]*\n', '', (ROOT/'src/activities/network/NetworkUiBinding.cpp').read_text(), flags=re.M)
binding = "namespace QrUtils {void drawQrCode(const GfxRenderer&, const Rect&, const std::string&) {std::abort();}}\n" + binding
source = source.replace("// SHARED_BINDING", header+"\n"+binding)
Path(sys.argv[1]).write_text(source)
