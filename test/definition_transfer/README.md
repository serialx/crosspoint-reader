# Definition selection transfer checks

These host checks exercise the Definition-only post-pop owned result transfer.
`composition.py` links the production Rust App and extracts the production native
bridge and Definition functions, with physical Page, lock and viewer fakes. Its
30 ASan/UBSan cases assert event order, allocation identity and failure cleanup.
`negative_controls.py` rejects five ordering/provenance mutants and runs an
unmodified Miri control. `layout.py` compiles C++/Rust wire-layout assertions for
the host and ESP32-C3. These checks do not exercise a physical device.

Run: from the repository root with Python 3, a C++20 compiler with ASan/UBSan,
the Rust host toolchain, nightly Miri and the C3 toolchains installed:

```sh
cmake -S test -B /tmp/crosspoint-definition-host
cmake --build /tmp/crosspoint-definition-host --target xp_ffi_host_build
definition_host=$(rustc -vV | sed -n 's/^host: //p')
definition_archive="/tmp/crosspoint-definition-host/xp-ffi-host-target/${definition_host}/release/libxp_ffi.a"
definition_evidence=/tmp/crosspoint-definition-evidence

python3 test/definition_transfer/composition.py \
  --archive "$definition_archive" --evidence "$definition_evidence/compositions"
python3 test/definition_transfer/negative_controls.py \
  --archive "$definition_archive" --evidence "$definition_evidence/negative-controls"
python3 test/definition_transfer/layout.py \
  --evidence "$definition_evidence/layout"
```

Each script writes its generated witnesses, logs and `results.json` under the
given evidence directory. The host archive must include `app-test`, which the
root test CMake configuration supplies. Never enable that feature in firmware.

These three scripts are **not registered with CMake/CTest**. Building
`xp_ffi_host_build` prepares their archive; the Python commands above run the
checks separately. A passing full CTest invocation does not include these three
script invocations. No CMake registration or build input is changed here.

Tooling limitation: `layout.py` currently hardcodes the C3 C++ compiler at
`/Users/serialx/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-g++`.
That path must exist for its C3 check. Making the path configurable remains a
test-tooling maintenance item; this documentation does not change the script.
