"""Compile native boundary assertions and check all six sections/capabilities."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SETTINGS = ROOT / 'src/activities/settings'


def block(source, pattern):
    return re.search(pattern, source, re.S).group(0)


def run(archive, mutate=False):
    native = block((SETTINGS / 'SettingsActivity.h').read_text(), r'enum class SettingAction \{.*?\};')
    if mutate:
        native = native.replace('  Language,', '  InsertedAction,\n  Language,')
    section = block((SETTINGS / 'SettingsSection.h').read_text(), r'enum class SettingsSection.*?\};')
    activity = (SETTINGS / 'SettingsActivity.cpp').read_text()
    assertions = '\n'.join(re.findall(r'static_assert\(.*?\);', activity, re.S))
    labels = block(activity, r'static constexpr StrId actionLabels\[\] = \{.*?\};')
    label_names = re.findall(r'StrId::(\w+)', labels)
    source = '#include <cassert>\n#include <cstdint>\n#include <vector>\n#include "xp_ui_settings.h"\n'
    source += native + section + assertions
    source += 'enum class StrId {' + ','.join(label_names) + '};\n' + labels
    for file, namespace in [('ClockSettingsActivity.cpp', 'clock_menu'), ('StatusBarSettingsActivity.cpp', 'status_menu')]:
        text = (SETTINGS / file).read_text()
        source += '\nnamespace ' + namespace + '{\n' + block(text, r'enum MenuItem \{.*?\};')
        source += '\n'.join(re.findall(r'static_assert\(static_cast<int>\(ITEM_.*?\);', text, re.S)) + '\n}\n'
    # Expected action/label pairs and order frozen from bcdfb338 rebuildSettings.
    names = ['None','RemapFrontButtons','CustomiseStatusBar','ClockSettings','KOReaderSync','OPDSBrowser',
             'Network','Bluetooth','ClearCache','CheckForUpdates','SdFirmwareUpdate','Language','DownloadFonts',
             'TextSettings','Plugins','KeyboardLayouts','HomeButton','About']
    expected_labels = ['NONE_OPT','REMAP_FRONT_BUTTONS','CUSTOMISE_STATUS_BAR','CLOCK','KOREADER_SYNC','OPDS_SERVERS',
                       'WIFI_NETWORKS','BT_PAGE_TURNER','CLEAR_READING_CACHE','CHECK_UPDATES','SD_FIRMWARE_UPDATE',
                       'LANGUAGE','MANAGE_FONTS','TEXT_SETTINGS','PLUGINS','KEYBOARD_LAYOUTS','HOME_BUTTON','ABOUT']
    source += '\nint main() {\n'
    for i, (name, label) in enumerate(zip(names, expected_labels)):
        source += f'assert(static_cast<int>(SettingAction::{name}) == {i}); assert(actionLabels[{i}] == StrId::STR_{label});\n'
    source += r'''
    for (int caps = 0; caps < 64; ++caps) {
      std::vector<int> general{11, 15, 255};
      if (caps & 2) general.push_back(3);
      if (!(caps & 4)) { general.push_back(9); general.push_back(10); }
      general.push_back(17);
      std::vector<int> controls;
      if (caps & 8) controls.push_back(16);
      if (!(caps & 16)) controls.push_back(1);
      controls.push_back(255);
      std::vector<int> network{255, 6};
      if (caps & 32) network.push_back(7);
      for (int action : {4, 5, 14}) network.push_back(action);
      const std::vector<int> sections[] = {general, {255}, {13,12,255,2,8}, controls, {255}, network};
      for (int section = 0; section < 6; ++section) {
        int index = 0;
        for (int expected : sections[section]) assert(xp_ui_settings_extra(section, caps, index++) == expected);
        assert(xp_ui_settings_extra(section, caps, index) == 0);
      }
    }
    for (int value = 0; value < 256; ++value) {
      for (int item = 0; item < 5; ++item) {
        auto result = xp_ui_settings_menu_edit(XP_UI_SETTINGS_CLOCK, item, value);
        int kind = item == 0 || item == 4 ? XP_UI_SETTINGS_OPEN : XP_UI_SETTINGS_SAVE_VALUE;
        int next = item == 0 || item == 4 ? item : item == 1 ? (value + 1) % 3 : uint8_t(value + 1) % 2;
        assert(result.kind == kind && result.value == next);
      }
      for (int item = 0; item < 8; ++item) {
        auto result = xp_ui_settings_menu_edit(XP_UI_SETTINGS_STATUS_BAR, item, value);
        bool popup = item == 2 || item == 3 || item == 4 || item == 6;
        int next = popup ? value : item == 7 ? (value + 1) % 3 : uint8_t(value + 1) % 2;
        assert(result.kind == (popup ? XP_UI_SETTINGS_POPUP : XP_UI_SETTINGS_SAVE_VALUE) && result.value == next);
        int normalized = value;
        if (value >= 3) {
          if (item == 2 || item == 4) normalized = 2;
          if (item == 3) normalized = 1;
          if (item == 6 || item == 7) normalized = 0;
        }
        assert(xp_ui_settings_status_value(item, value) == normalized);
      }
    }
    }
    '''
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / 'protocol.cpp'; cpp.write_text(source)
        exe = Path(directory) / 'protocol'
        result = subprocess.run(['c++', '-std=c++20', '-I', str(ROOT / 'rust/crates/xp-ffi/include'), str(cpp),
                                 str(ROOT / 'test/cmake/xp_ffi_host.cpp'), archive, '-o', str(exe)], capture_output=True, text=True)
        if mutate:
            assert result.returncode != 0 and 'static assertion failed' in result.stderr, result.stderr
        else:
            assert result.returncode == 0, result.stderr
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    run(sys.argv[1]); run(sys.argv[1], True)
    print('384 section/capability cases; 3328 menu edits; 2048 normalizations; native reorder rejected')
