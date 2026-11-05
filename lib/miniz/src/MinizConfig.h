/* The pinned SDK uses only mz_inflateInit2, mz_inflate(NO_FLUSH), and
 * mz_inflateEnd. MinizCompat.cpp supplies that subset through Rust; retain
 * the public mz_stream declarations for ABI compatibility. */
#pragma once

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES

// ESP-IDF ROM exports these names with absolute linker assignments.
#define mz_crc32 crosspoint_mz_crc32
#define mz_adler32 crosspoint_mz_adler32

// Keep the mz_stream layout consistent with the pinned SDK header.
#include "../third_party/miniz.h"
