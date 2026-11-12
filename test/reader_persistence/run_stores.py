#!/usr/bin/env python3
import argparse, os, pathlib, re, subprocess
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
p = argparse.ArgumentParser()
p.add_argument('--build', type=pathlib.Path, required=True)
p.add_argument('--arduinojson', type=pathlib.Path, required=True)
a = p.parse_args(); a.build.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, CARGO_TARGET_DIR=str(a.build/'rust'))
subprocess.run(['cargo', 'build', '--offline', '--manifest-path', str(HERE/'stores/Cargo.toml')], env=env, check=True)
source = (ROOT/'src/activities/reader/RustEpubResources.cpp').read_text()
start = re.search(r'\.store_document\s*=\s*', source).end()
end = source.index(',\n#endif', start)
(a.build/'store_document.inc').write_text('const auto document = ' + source[start:end] + ';\n')
flags = ['-std=c++20', '-O2', '-UNDEBUG', '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0', '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0', '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0']
includes = [HERE/'stubs', ROOT/'test/settings_store/stubs', ROOT/'src', ROOT/'lib/Serialization', ROOT/'rust/crates/xp-ffi/include', a.arduinojson, a.build]
sources = [HERE/'Stores.cpp', ROOT/'src/RecentBooksStore.cpp', ROOT/'src/CrossPointState.cpp', ROOT/'lib/Serialization/PersistableStore.cpp', a.build/'rust/debug/libreader_store_tests.a']
subprocess.run(['c++', *flags, *[f'-I{x}' for x in includes], *map(str, sources), '-o', str(a.build/'stores')], check=True)
subprocess.run([str(a.build/'stores')], check=True)
