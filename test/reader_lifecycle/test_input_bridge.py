"""Check the actual native mapped-input bridge, including every owner button mask."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition

LOGGER = r'''
#include <cassert>
#include <cstdarg>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>
static std::vector<std::string> probes;
static int bridgePrintf(const char* format, ...) {
  char text[128]; va_list args; va_start(args, format);
  int size = std::vsnprintf(text, sizeof(text), format, args); va_end(args);
  assert(size >= 0 && size_t(size) < sizeof(text));
  probes.emplace_back(text, size); return size;
}
static int bridgePuts(const char* text) { probes.emplace_back(std::string(text) + "\n"); return 0; }
static void require(bool condition, const std::string& marker) {
  if (!condition) throw std::runtime_error(marker);
}
'''

MAIN = r'''
int main() {
  try {
    using B = MappedInputManager::Button;
    struct Mapping { uint32_t mask; B button; const char* name; };
    const Mapping mappings[] = {
      {XP_UI_BACK, B::Back, "Back"}, {XP_UI_CONFIRM, B::Confirm, "Confirm"},
      {XP_UI_UP, B::Up, "Up"}, {XP_UI_DOWN, B::Down, "Down"},
      {XP_UI_LEFT, B::Left, "Left"}, {XP_UI_RIGHT, B::Right, "Right"},
      {XP_UI_POWER, B::Power, "Power"}, {XP_UI_PAGE_BACK, B::PageBack, "PageBack"},
      {XP_UI_PAGE_FORWARD, B::PageForward, "PageForward"}
    };
    unsigned cases = 0;
    for (const auto& m : mappings) {
      for (bool active : {false, true}) {
        MappedInputManager input;
        const uint32_t bit = 1u << int(m.button);
        input.pressed = input.released = input.held = active ? bit : 0;
        input.heldMs = 700;
        const auto services = ReaderUtils::inputServices(input);
        assert(services.context == &input);
        for (auto query : {XP_UI_READER_BUTTON_PRESS, XP_UI_READER_BUTTON_RELEASE}) {
          probes.clear();
          const auto result = services.button(services.context, m.mask, query, 123);
          const std::string tag = query == XP_UI_READER_BUTTON_PRESS ? "press" : "release";
          const auto expected = tag + " " + std::to_string(int(m.button)) + " " +
                                std::to_string(active) + "\n";
          require(result == active && probes == std::vector<std::string>{expected},
                  std::string("BRIDGE_MASK_") + m.name);
          ++cases;
        }
        for (unsigned threshold : {701u, 700u, 700u}) {
          const bool expected = active && threshold == 700 && !input.fired;
          probes.clear();
          const auto result = services.button(services.context, m.mask, XP_UI_READER_BUTTON_LONG, threshold);
          std::vector<std::string> trace;
          if (active && !(input.fired && !expected)) trace.emplace_back("held 700\n");
          trace.push_back("long " + std::to_string(int(m.button)) + " " +
                          std::to_string(threshold) + " " + std::to_string(expected) + "\n");
          require(result == expected && probes == trace, std::string("BRIDGE_LONG_") + m.name);
          ++cases;
        }
        probes.clear();
        require(services.button(services.context, m.mask, XP_UI_READER_BUTTON_RELEASE, 0) == 0,
                std::string("BRIDGE_SUPPRESSED_") + m.name);
        require(probes == std::vector<std::string>{"release " + std::to_string(int(m.button)) + " 0\n"},
                "BRIDGE_RELEASE_PROBE");
        probes.clear();
        require(services.button(services.context, m.mask, 255, 0) == 0 && probes.empty(), "BRIDGE_UNKNOWN_QUERY");
      }
    }
    MappedInputManager input;
    const auto services = ReaderUtils::inputServices(input);
    for (uint32_t mask : {0u, uint32_t(XP_UI_CONFIRM | XP_UI_UP), uint32_t(1u << 31)})
      for (auto query : {XP_UI_READER_BUTTON_PRESS, XP_UI_READER_BUTTON_RELEASE, XP_UI_READER_BUTTON_LONG}) {
        probes.clear();
        require(services.button(services.context, mask, query, 700) == 0 && probes.empty(), "BRIDGE_UNKNOWN_MASK");
      }
    auto query = [&](uint8_t id, uint32_t expected, const char* trace) {
      probes.clear();
      require(services.read(services.context, id) == expected &&
              probes == std::vector<std::string>{trace}, "BRIDGE_INPUT_QUERY_" + std::to_string(id));
    };
    input.touch = input.swapped = input.backGesture = input.menuGesture = input.menuSwipeUp = true;
    input.home = HomeButtonAction::NextPage; input.heldMs = 700;
    halTiltSensor.next = halTiltSensor.prev = true;
    query(XP_UI_READER_INPUT_HAS_TOUCH, 1, "touch 1\n");
    query(XP_UI_READER_INPUT_HELD_MS, 700, "held 700\n");
    query(XP_UI_READER_INPUT_TOUCH_HELD_MS, 731, "touch-held\n");
    query(XP_UI_READER_INPUT_TILT_NEXT, 1, "tilt-next 1\n");
    query(XP_UI_READER_INPUT_TILT_PREV, 1, "tilt-prev 1\n");
    query(XP_UI_READER_INPUT_NAV_SWAPPED, 1, "swap 1\n");
    query(XP_UI_READER_INPUT_HOME_ACTION, 2, "home-action 2\n");
    query(XP_UI_READER_INPUT_BACK_GESTURE, 1, "back-gesture 1\n");
    require(input.touchOverride, "BRIDGE_BACK_TOUCH_OVERRIDE");
    query(XP_UI_READER_INPUT_MENU_GESTURE, 1, "menu-gesture 1\n");
    query(XP_UI_READER_INPUT_MENU_SWIPE_UP, 1, "menu-up 1\n");
    struct Swipe { MappedInputManager::SwipeDir native; uint32_t wire; };
    for (const auto swipe : {Swipe{MappedInputManager::SwipeDir::None, XP_UI_SWIPE_NONE},
                            Swipe{MappedInputManager::SwipeDir::Left, XP_UI_SWIPE_LEFT},
                            Swipe{MappedInputManager::SwipeDir::Right, XP_UI_SWIPE_RIGHT},
                            Swipe{MappedInputManager::SwipeDir::Up, XP_UI_SWIPE_UP},
                            Swipe{MappedInputManager::SwipeDir::Down, XP_UI_SWIPE_DOWN}}) {
      input.swipe = swipe.native;
      const auto trace = "swipe " + std::to_string(int(swipe.native)) + "\n";
      query(XP_UI_READER_INPUT_SWIPE, swipe.wire, trace.c_str());
    }
    int32_t x = -1, y = -2;
    probes.clear();
    require(!services.tap(services.context, &x, &y) && x == -1 && y == -2 &&
            probes == std::vector<std::string>{"tap 0\n"}, "BRIDGE_TAP_MISS");
    input.tapped = true; input.touchOverride = false; input.x = 33; input.y = 44;
    probes.clear();
    require(services.tap(services.context, &x, &y) && x == 33 && y == 44 && input.touchOverride &&
            probes == std::vector<std::string>{"tap 1\n"}, "BRIDGE_TAP_HIT");
    probes.clear();
    require(services.read(services.context, 255) == 0 && probes.empty(), "BRIDGE_UNKNOWN_INPUT_QUERY");
    std::printf("PASS input bridge buttons=9 action-cases=%u unknown-masks=3\n", cases);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "EXPECTED_ASSERTION: %s\n", error.what()); return 42;
  }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_INPUT_BRIDGE_EVIDENCE_DIR'))
    args = parser.parse_args()
    paths = [ROOT / 'src/activities/reader/ReaderUtils.h', HERE / 'input_oracle.cpp',
             ROOT / 'rust/crates/xp-ffi/include/xp_ui_reader.h', Path(__file__).resolve()]
    original = {path: path.read_bytes() for path in paths}
    goldens = {path: path.read_bytes() for path in HERE.glob('*.golden.gz')}
    method = definition(original[paths[0]].decode(), 'inline XpUiReaderInputServices inputServices(')
    template = original[paths[1]].decode().split('static void buttons() {', 1)[0]
    assert template.count('#include "reader_value_owner.h"') == 1
    template = template.replace('#include "reader_value_owner.h"', '#include <xp_ui_reader.h>')
    template = template.replace('std::printf(', 'bridgePrintf(').replace('std::puts(', 'bridgePuts(')
    assert template.count('#include "input_methods.inc"') == 1
    variants = [('baseline', method, None)]
    for mask, button in [('CONFIRM', 'Confirm'), ('UP', 'Up'), ('DOWN', 'Down')]:
        block = (f'              case XP_UI_{mask}:\n'
                 f'                button = MappedInputManager::Button::{button};\n'
                 '                break;\n')
        assert method.count(block) == 1
        variants.append((mask.lower() + '-removed', method.replace(block, ''), 'BRIDGE_MASK_' + button))
    with tempfile.TemporaryDirectory(prefix='reader-input-bridge-') as directory:
        evidence = args.evidence_dir or Path(directory) / 'evidence'
        evidence.mkdir(parents=True, exist_ok=False)
        records = []
        try:
            for name, body, marker in variants:
                source = LOGGER + template.replace('#include "input_methods.inc"', 'namespace ReaderUtils {\n' + body + '\n}') + MAIN
                cpp = evidence / (name + '.cpp'); cpp.write_text(source)
                binary = evidence / name
                command = ['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                           '-fno-sanitize-recover=all', '-I', str(ROOT / 'rust/crates/xp-ffi/include'),
                           str(cpp), '-o', str(binary)]
                (evidence / (name + '.command.json')).write_text(json.dumps(command, indent=2) + '\n')
                built = subprocess.run(command, capture_output=True)
                (evidence / (name + '.compile.stdout')).write_bytes(built.stdout)
                (evidence / (name + '.compile.stderr')).write_bytes(built.stderr)
                assert built.returncode == 0, built.stderr.decode(errors='replace')
                ran = subprocess.run([str(binary)], capture_output=True, timeout=30)
                (evidence / (name + '.stdout')).write_bytes(ran.stdout)
                (evidence / (name + '.stderr')).write_bytes(ran.stderr)
                if marker:
                    assert ran.returncode == 42 and ran.stderr == ('EXPECTED_ASSERTION: ' + marker + '\n').encode(), (name, ran.returncode, ran.stderr)
                else:
                    assert ran.returncode == 0 and b'PASS input bridge' in ran.stdout, (ran.returncode, ran.stderr)
                records.append({'name': name, 'returncode': ran.returncode, 'marker': marker})
            (evidence / 'result.json').write_text(json.dumps({'records': records, 'source_sha256': {
                str(path): hashlib.sha256(data).hexdigest() for path, data in original.items()}}, indent=2) + '\n')
        finally:
            assert all(path.read_bytes() == data for path, data in original.items())
            assert all(path.read_bytes() == data for path, data in goldens.items())


if __name__ == '__main__':
    main()
