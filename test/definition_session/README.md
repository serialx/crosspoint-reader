# Definition ownership tests

Run commands from the repository root. Host C++ tests need CMake, a C++20
compiler, AddressSanitizer/UndefinedBehaviorSanitizer and the Rust host archive
built by the test configuration. Cargo commands use the checked-in lockfiles
and offline dependencies. Manual Miri checks also need a nightly toolchain with
Miri installed.

## Registered host checks

After configuring and building the host suite, run:

```sh
ctest --test-dir /tmp/crosspoint-host --output-on-failure -R '^reader_definition_'
```

Replace `/tmp/crosspoint-host` with your configured CMake build directory.
`test/reader_children/CMakeLists.txt` registers four checks:

- `reader_definition_owner`: actual App/Definition owner, navigation, failed
  installation, heap choice, header input, render and frontlight-return cases.
- `reader_definition_owned_text`: allocation, transfer and release of owned text.
- `reader_definition_native_payload`: `payload.py` compares 48 geometry,
  wrapping and transfer cases with retained native reference methods.
- `reader_definition_app_result`: `composition.py` runs eight actual-App/C++
  result compositions, including success/cancel, destruction/publication/unlock,
  temporary-Page release, one copy and native result lifetime.

The payload harness supplies HTML and physical-service stubs. Its 48 cases do
not establish end-to-end coverage of malformed HTML, the >16 KiB input boundary
or the 64-Page limit. `definition_oracle.py` verifies and loads the retained
native sections listed with their consumers in `native_oracle/manifest.json`;
it is a helper, not a standalone test.

To run either C++ harness separately, supply the same host archive used by CMake:

```sh
python3 test/definition_session/payload.py --archive /path/to/libxp_ffi.a --evidence /tmp/definition-payload
python3 test/definition_session/composition.py --archive /path/to/libxp_ffi.a --evidence /tmp/definition-composition
```

## Manual Miri and layout checks

`negative.py` and `layout.py` are not registered in CTest. Run them explicitly:

```sh
MIRIFLAGS=-Zmiri-strict-provenance cargo +nightly miri test --offline --locked --manifest-path test/home_ui/app_owner/Cargo.toml --features app-test app::epub::
MIRIFLAGS=-Zmiri-strict-provenance cargo +nightly miri test --offline --locked --manifest-path test/definition_session/Cargo.toml
python3 test/definition_session/negative.py --evidence /tmp/definition-negative-new
python3 test/definition_session/layout.py --evidence /tmp/definition-layout
```

The negative evidence directory must not already exist. The script makes an
isolated source copy and runs strict-provenance Miri itself. Its positive
control must pass; four mutants must fail: removing the frontlight-result
filter, keeping a protected whole-owner reference across destruction, querying
the wrong heap, and publishing a failed prepare's unspecified output. A zero
script exit means the control and all expected rejections matched.

`layout.py` compiles C++ and Rust size/offset assertions for the host and
`riscv32imc-unknown-none-elf`. It checks the private historical C wire in
`legacy_wire.h` against the live Rust representation and records source hashes.
It requires that Rust target and currently hardcodes the local C3 compiler at
`/Users/serialx/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-g++`.
Use that installed toolchain path or adapt the local invocation before running
on another machine; the script does not discover other PlatformIO homes.
