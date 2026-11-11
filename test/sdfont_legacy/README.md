# Historical native SD font adapters

The six SdCardFont, SdCardFontManager and SdCardFontRegistry source/header
files are byte-exact copies of production at `7bab9148`, preserved when CP2c
deleted the unlinked native implementations. Host differential and ABI
tests retain them as historical adapters; firmware must not include this directory.
They are not the shipping face, cache, manager or catalog implementation.

Shipping ownership is covered by `SdFontOwnerSwitch`, `SdFontSystemSwitch`,
`SdFontCatalogFrontDoor` and `SdFontXtcOrder`. The `abi/` headers declare the
host-only parser, cache, registry and manager interfaces; `gfx_sd_oracle.inc`
contains renderer adapters used only by historical fixtures. Rust oracle exports
are gated by `app-test`; these declarations are not part of the shipping ABI. The shipping vector stream adapter is the non-owning FreeType leaf in
`lib/hal/VectorFontPhysical.cpp`.
