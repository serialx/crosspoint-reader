These reduced GNU nm/readelf text fixtures use the default-X3 ELF32 record format.
They are synthetic excerpts, not host Mach-O objects or a complete firmware dump.
`RustBidi.cpp.o` contains both a clean Rust import and a wrapper called from the
keyboard port. The regression test adds workspace-size/alignment references to
the wrapper and verifies that the per-function walk catches them across objects.
