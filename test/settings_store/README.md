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

The runner links the generated translation unit and exported language metadata;
it does not parse generated C++ definitions. After the i18n port, the Rust archive
supplies the same metadata symbols. A private ArduinoJson copy counts linked-list
advances during token and Wi-Fi loads with 256–4,096 rejected rows before a valid
row. The linear operation bound catches repeated indexed traversal independently
of host timing. `scaling-golden.json.gz`, captured from the original C++ with
`--record-scaling`, also checks saved bytes, diagnostics and JSON allocation traces
for those 15 inputs; the original 2,529 golden cases remain unchanged.

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
