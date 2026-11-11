# Historical native SD font adapters

These six files are copied byte-for-byte from production at `7bab9148` before
CP2c deletes the unlinked native implementations. Host differential and ABI
tests retain them as historical adapters; firmware must not include this directory.
They are not the shipping face, cache, manager or catalog implementation.

Shipping ownership is covered by `SdFontOwnerSwitch`, `SdFontSystemSwitch`,
`SdFontCatalogFrontDoor` and `SdFontXtcOrder`. CP2d owns isolation or retirement
of the legacy ABI these adapters use. The live vector physical read callback
remains in `lib/EpdFont/SdCardFontRegistry.cpp` under PT-VEC.
