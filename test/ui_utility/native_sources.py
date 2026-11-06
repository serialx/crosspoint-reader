"""Frozen native contracts: source data only, never compiler/include inputs."""
from pathlib import Path
import hashlib
import json
import re

HERE = Path(__file__).resolve().parent
ORACLES = HERE / 'native_oracle'
manifest = json.loads((ORACLES / 'manifest.json').read_text())
texts = {}
for row in manifest['inputs']:
    data = (ORACLES / row['file']).read_bytes()
    assert len(data) == row['bytes']
    assert hashlib.sha256(data).hexdigest() == row['sha256']
    assert row['consumers'] and all((HERE / x).is_file() for x in row['consumers'])
    texts[row['file'].removesuffix('.txt')] = data.decode()
assert set(p.name for p in ORACLES.glob('*.txt')) == {r['file'] for r in manifest['inputs']}
base = texts['UtilityActivity.cpp']
assert 'commandCapacity * sizeof(XpUiCommand) + 2 * hitCapacity * sizeof(XpUiInteraction)' in base
assert base.index('arena = makeUniqueNoThrow') < base.index('xp_ui_utility_init') < base.index('requestUpdate(')
assert base.index('arena.reset()') < base.index('  Activity::onExit();')
assert 'buildHits ^= 1' in base
assert 'result.isCancelled = true;' in base and 'setResult(ActivityResult{});' in base
assert 'const uint16_t commands, const uint16_t hits' in base
assert 'Publication publication{};' in texts['UtilityActivity.h']
assert 'std::unique_ptr<uint8_t[]> arena;' in texts['UtilityActivity.h']
for name, commands, hits in [('Confirmation', 10, 3), ('IntervalSelection', 18, 5)]:
    text = texts[name + 'Activity.cpp']
    assert re.search(r'XP_UI_UTILITY_\w+,\s*' + str(commands) + r',\s*' + str(hits) + r'\)', text)
    assert 'public UtilityActivity' in texts[name + 'Activity.h']
assert 'std::string heading, body, cancelLabel, confirmLabel;' in texts['ConfirmationActivity.h']
assert 'cancelLabel.empty() ? tr(STR_CANCEL)' in texts['ConfirmationActivity.cpp']
assert 'confirmLabel.empty() ? tr(STR_CONFIRM)' in texts['ConfirmationActivity.cpp']
assert 'char readout[64]{}, hints[2][64]{}, minText[32]{}, maxText[32]{};' in texts['IntervalSelectionActivity.h']
assert 'IntervalResult{static_cast<uint32_t>(effect.navigation.value)}' in texts['IntervalSelectionActivity.cpp']
assert 'char stepText[24];' in texts['IntervalSelectionActivity.cpp']
message = texts['FullScreenMessageActivity.cpp']
assert 'XpUiCommand commands[2]{};' in message
assert 'paintRustScene(scene);' in message and 'finish()' not in message
assert 'void loop() override {}' in texts['FullScreenMessageActivity.h']
assert 'void render(RenderLock&&) override {}' in texts['FullScreenMessageActivity.h']
assert ': RustActivityAdapter("FullScreenMessage"' in texts['FullScreenMessageActivity.h']
cmake = (HERE / 'CMakeLists.txt').read_text()
assert not re.search(r'(?:include_directories|add_executable|add_library)[^)]*native_oracle', cmake, re.S)
print(f"8 native contracts, {sum(row['bytes'] for row in manifest['inputs'])} frozen bytes")
