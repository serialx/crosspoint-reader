#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build_dir="${TMPDIR:-/tmp}/crosspoint-clipping-sync-tests"
json_dir="${ARDUINOJSON_DIR:-.pio/libdeps/default/ArduinoJson/src}"
mkdir -p "$build_dir"
rust_archive="${XP_TEST_RUST_ARCHIVE:-build/rust-host/release/libxp_ffi.a}"
if [ -z "${XP_TEST_RUST_ARCHIVE:-}" ]; then
  cargo build --manifest-path rust/Cargo.toml --target-dir build/rust-host \
    -p xp-ffi --features app-test --release --locked
fi
for chip in ESP32C3 ESP32S3; do
  c++ -std=c++20 -Wall -Wextra -Werror -Wno-unused-parameter -fno-exceptions \
    "-DCONFIG_IDF_TARGET_$chip=1" -I"$json_dir" \
    -Itest/clipping_sync/stubs -Itest/clipping_store/stubs -Itest/library_builder/stubs \
    -Itest -Itest/reader_lifecycle -Irust/crates/xp-ffi/include \
    -Isrc -Ilib/Memory -Ilib/Serialization -Ilib/Utf8 \
    test/clipping_sync/ClippingSyncTest.cpp src/clippings/ClippingSync.cpp \
    src/ClippingStore.cpp lib/Utf8/Utf8.cpp lib/Memory/BuildScratch.cpp \
    test/cmake/xp_ffi_host.cpp "$rust_archive" \
    -o "$build_dir/$chip"
  "$build_dir/$chip"
done
