"""Compare HAL leaf, failure, lock, byte and diagnostic traces with the C++ oracle."""
import argparse
import gzip
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SOURCES = ['DeviceSecret/DeviceSecret.cpp', 'TrustedTime/TrustedTime.cpp',
           'hal/HalSystem.cpp', 'hal/HalStorage.cpp', 'hal/HalDisplay.cpp', 'hal/HalGPIO.cpp', 'hal/HalClock.cpp', 'hal/HalFrontlight.cpp', 'hal/HalTiltSensor.cpp']
CASES = {'secret': 7, 'time': 5, 'light': 3, 'tilt': 5, 'clock': 6, 'power': 4, 'gpio': 8, 'display': 4, 'storage': 10, 'system': 6, 'haptics': 2}

def run(root, archive):
    with tempfile.TemporaryDirectory(prefix='hal-policy-') as tmp:
        exe = str(Path(tmp) / 'check')
        # ESP32 unsigned long is 32-bit; macOS uses 64-bit long. Normalize only
        # the firmware source copies so wraparound timer traces model the target.
        for directory in ['hal', 'DeviceSecret', 'TrustedTime']:
            for header in (root / 'lib' / directory).glob('*.h'):
                (Path(tmp) / header.name).write_text(header.read_text().replace('unsigned long', 'uint32_t'))
        command = ['c++', '-std=c++20', '-DFREEINK_MCU_C3=1', '-D__riscv=1', '-DCROSSPOINT_VERSION="oracle"', '-DFREEINK_CAP_HAPTIC=1', '-DFREEINK_CAP_USB_MSC=1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-private-field', '-Wno-int-to-pointer-cast', '-Wno-sign-compare',
                   '-I' + str(HERE / 'stubs'), '-I' + tmp, '-I' + str(root / 'rust/crates/xp-ffi/include')]
        command += ['-I' + str(root / 'lib' / d) for d in ['hal', 'DeviceSecret', 'TrustedTime']]
        power = (root / 'lib/hal/HalPowerManager.cpp').read_text()
        start = power.index('void HalPowerManager::startDeepSleep')
        end = power.index('uint16_t HalPowerManager::getBatteryPercentage')
        power = power[:start] + power[end:]
        power = power.replace('static constexpr gpio_num_t XTEINK_C3_GPIO13 = GPIO_NUM_13;', '')
        power_file = Path(tmp) / 'power.cpp'
        power_file.write_text(power.replace('unsigned long', 'uint32_t'))
        command += [str(power_file)]
        for source in SOURCES:
            source_file = Path(tmp) / Path(source).name
            source_file.write_text((root / 'lib' / source).read_text().replace('unsigned long', 'uint32_t').replace('static_cast<long>', 'static_cast<int32_t>'))
            command.append(str(source_file))
        command += [str(HERE / 'check.cpp')]
        if archive:
            command += [str(ROOT / 'test/cmake/xp_ffi_host.cpp'), archive]
        subprocess.run(command + ['-o', exe], check=True)
        return {f'{name}/{v}': subprocess.check_output([exe, name, str(v)], text=True)
                for name, count in CASES.items() for v in range(count)}

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--archive', default=os.environ.get('XP_TEST_RUST_ARCHIVE'))
    args = parser.parse_args()
    results = run(args.root, args.archive)
    golden = HERE / 'golden.json.gz'
    if args.capture:
        with gzip.open(golden, 'wt') as f:
            json.dump(results, f, sort_keys=True)
    else:
        with gzip.open(golden, 'rt') as f:
            expected = json.load(f)
        for key, value in results.items():
            if value != expected[key]:
                import difflib
                print(''.join(difflib.unified_diff(expected[key].splitlines(True), value.splitlines(True), fromfile=key+' C++', tofile=key+' current')))
                raise SystemExit(1)
    print(f'{len(results)} HAL policy traces passed')
