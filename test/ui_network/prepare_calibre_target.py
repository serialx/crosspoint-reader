from pathlib import Path
import sys
import prepare
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
source=(HERE/'CalibreTarget.cpp').read_text()
original=(HERE/'calibre_render_original.inc').read_text().replace('CalibreConnectActivity::render','Original::render')
production=(ROOT/'src/activities/network/CalibreConnectActivity.cpp').read_text()
methods='\n'.join(prepare.extract(production,'CalibreConnectActivity',m) for m in ['text','buildRustScene','rustResources'])
source=source.replace('// ORIGINAL_RENDER',original).replace('// PRODUCTION_METHODS',methods).replace('millis()', 'testNow')
Path(sys.argv[1]).write_text(source)
