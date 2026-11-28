#pragma once
// SdFat oflag names the extraction targets use. The fixture interprets the bits
// (read / create / truncate / append / read-write), so only the store's choice
// of access mode is modelled, not the numeric value SDCardManager receives.
using oflag_t = unsigned;
constexpr oflag_t O_RDONLY = 0;
constexpr oflag_t O_WRITE = 1;
constexpr oflag_t O_WRONLY = 1;
constexpr oflag_t O_RDWR = 2;
constexpr oflag_t O_APPEND = 8;
constexpr oflag_t O_CREAT = 0x40;
constexpr oflag_t O_TRUNC = 0x100;
constexpr oflag_t O_AT_END = 0x200;
