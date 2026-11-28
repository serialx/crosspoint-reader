#pragma once
using oflag_t = unsigned;
constexpr oflag_t O_RDONLY = 0, O_WRITE = 1, O_CREAT = 2, O_TRUNC = 4;
constexpr oflag_t O_RDWR = 8, O_EXCL = 16;
