# Section I/O oracle

This suite captures the production Section implementation at `fc688443` before
its Rust orchestration port. It exercises full builds, partial suspension,
resumption, abandonment, lookup results, diagnostics, ordered file operations,
and every resulting cache byte. Each filesystem operation reached by each
successful scenario is then failed individually, including rename and remove.

Configure and build through the main `test/` CMake project. Run
`ctest --test-dir build/test -R 'SectionIoGolden|BookPolicyGolden' --output-on-failure`.
`SECTION_IO_SOURCE` can select an exported Section.cpp for a mutation check;
changing `builtPageCount_++` to `builtPageCount_ += 2` must fail the oracle.

The repository golden stores SHA-256 hashes of complete traces and file bytes
to keep the fixture small. Full baseline records are in the round evidence at
`p9-n-book-b/section-io-full.json.gz`. Hashes are compared independently for
each trace and each file; the file-name set and process result must also match.
Use `--record` only for an approved baseline capture.

The policy oracle also captures layout signatures, link hit boundaries, image
dimension limits and diagnostics, visible tags, and all named HTML entities.

The storage double uses host stdio and a single failed operation, not SD timing
or mutex contention. Rendering uses the existing Section synthetic font stub;
real-font page goldens and emulator screenshots remain separate requirements.
Allocation failures and persistent/repeated filesystem failure require the
existing page/chapter allocation suites and additional targeted tests.
