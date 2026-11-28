"""Extract unchanged production status drawing and the facts leaf; fail on source drift."""
from pathlib import Path
import sys

root, out = map(Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(root / 'test/reader_lifecycle'))
from test_popup_resource import definition, native_leaf

settings = (root / 'src/CrossPointSettings.h').read_text()
parts = ['struct CrossPointSettings {']
for name in ('STATUS_BAR_PROGRESS_BAR', 'STATUS_BAR_TITLE',
             'STATUS_BAR_CLOCK_MODE', 'XTC_STATUS_BAR_MODE'):
    parts.append(definition(settings, 'enum ' + name + ' {') + ';')
parts += [
    definition(settings, 'struct StatusBarSpec {') + ';',
    'StatusBarSpec value; StatusBarSpec statusBarSpec() const {return value;} }; inline CrossPointSettings SETTINGS;',
]
(out / 'status_spec.inc').write_text('\n'.join(parts))

source = (root / 'src/components/themes/BaseTheme.cpp').read_text()
parts = [source[source.index('constexpr int bookmarkStatusIconWidth'):source.index('}  // namespace')]]
for signature in ('void BaseTheme::drawBatteryOutline(',
                  'void BaseTheme::drawBatteryLightningBolt(',
                  'void BaseTheme::fillBatteryIcon(',
                  'void BaseTheme::drawBatteryLeft(',
                  'void BaseTheme::drawStatusBar('):
    parts.append(definition(source, signature))
parts.append(definition((root / 'src/components/themes/lyra/LyraTheme.cpp').read_text(),
                        'void LyraTheme::fillBatteryIcon('))
source = (root / 'src/components/UITheme.cpp').read_text()
parts.extend(definition(source, signature) for signature in
             ('int UITheme::getStatusBarHeight()', 'int UITheme::getProgressBarHeight()'))
parts.append('static auto readFacts = ' + native_leaf(
    (root / 'src/activities/reader/RustEpubResources.cpp').read_text(), 'status_facts') + ';')
(out / 'status_native.inc').write_text('\n'.join(parts))
