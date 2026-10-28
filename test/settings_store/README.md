# Persistence compatibility oracle

The golden file was captured from CrossPoint `539ebafc` before the Rust port.
`run.py` compiles the actual store implementations with a memory filesystem,
a fixed synthetic device key, and the installed mbedTLS base64 implementation.
No private credentials, books, or device files are used.

The 2,529 cases cover JSON truncations and mutations, legacy settings, invalid
credential lengths/checksums, token limits, mutation/write sequences with failed
writes, buffered reads/writes/seeks, JSON allocation failures, and buffer allocation
failure fallback. They compare serialized bytes, state, diagnostic text, storage
call order, and the JSON
allocator's allocation sequence and peak during store serialization.

Settings tests use a fixed descriptor list containing each persistence metadata
shape. They compile the actual settings migrations and BLE JSON helpers, with both
home-button capability values. Full board-specific SettingsList and settings-screen coverage must run in the
firmware/O2 checks; these synthetic descriptors do not replace them.

CMake registers `settings_store_oracle` when ArduinoJson and mbedTLS sources are
available. Set `STORE_TEST_ARDUINOJSON` to the ArduinoJson `src` directory and
`STORE_TEST_MBEDTLS` to the mbedTLS root if they are outside the usual PlatformIO
locations. The normal host build supplies the Rust archive.

For a standalone run:

```sh
cargo build --manifest-path rust/Cargo.toml -p xp-ffi --release --locked
python3 test/settings_store/run.py \
  --build build/settings-oracle \
  --arduinojson .pio/libdeps/default/ArduinoJson/src \
  --mbedtls "$HOME/.platformio/packages/framework-espidf/components/mbedtls/mbedtls" \
  --archive rust/target/release/libxp_ffi.a
```

`--source` can select an exported original checkout for a differential run.
`--record` is for capturing that original implementation only; do not regenerate
the golden file from the implementation under test. A one-bit change to the old
CRC polynomial is detected in the Wi-Fi fixture and in the obfuscation cases.
