from pathlib import Path
import sys
import prepare
HERE = Path(__file__).resolve().parent
source = (HERE/'WifiTarget.cpp').read_text()
frozen = (HERE/'wifi_render_original.inc').read_text()
methods = '\n'.join(prepare.extract(frozen, 'WifiSelectionActivity', name) for name in ['buildListScreen', 'buildPromptDialog'])
Path(sys.argv[1]).write_text(source.replace('// ORIGINAL_BODY', methods))
