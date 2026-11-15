"""Compile frozen native percent/host/repeat methods with the pinned SDK router."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reader_lifecycle'))
from extract import definition

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "test/reader_children"))
from oracle_sources import native_text
methods = []
for file, signatures in [
    ('src/activities/reader/EpubReaderPercentSelectionActivity.cpp', [
        'void EpubReaderPercentSelectionActivity::' + name + '('
        for name in ['adjustPercent', 'setPercent', 'onSliderEvent', 'onStepEvent', 'onOkEvent', 'cancel', 'confirm', 'loop']]),
    ('src/components/UiAppHost.cpp', ['UiAppHost::TouchRoute UiAppHost::routeTouch(', 'fui::ActionEvent UiAppHost::route(']),
    ('src/util/ButtonNavigator.cpp', ['void ButtonNavigator::onPressAndContinuous(', 'void ButtonNavigator::onPress(', 'void ButtonNavigator::onContinuous(', 'bool ButtonNavigator::shouldNavigateContinuously(']),
]:
    source = native_text(file)
    methods.extend(definition(source, signature) for signature in signatures)
Path(sys.argv[1]).write_text((Path(__file__).with_name('percent_native.cpp.in')).read_text().replace('@METHODS@', '\n'.join(methods)))
