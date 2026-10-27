/**************************************************************************
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/



#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long mz_ulong;
typedef uint8_t mz_uint8;
typedef uint32_t mz_uint32;
typedef void* (*mz_alloc_func)(void*, size_t, size_t);
typedef void (*mz_free_func)(void*, void*);

#define MZ_ADLER32_INIT 1
#define MZ_CRC32_INIT 0
#define MZ_DEFAULT_WINDOW_BITS 15

enum { MZ_NO_FLUSH = 0, MZ_PARTIAL_FLUSH = 1, MZ_SYNC_FLUSH = 2, MZ_FULL_FLUSH = 3, MZ_FINISH = 4, MZ_BLOCK = 5 };
enum { MZ_OK = 0, MZ_STREAM_END = 1, MZ_NEED_DICT = 2, MZ_ERRNO = -1, MZ_STREAM_ERROR = -2,
       MZ_DATA_ERROR = -3, MZ_MEM_ERROR = -4, MZ_BUF_ERROR = -5, MZ_VERSION_ERROR = -6, MZ_PARAM_ERROR = -10000 };

    struct mz_internal_state;

    /* Compression/decompression stream struct. */
    typedef struct mz_stream_s
    {
        const unsigned char *next_in; /* pointer to next byte to read */
        unsigned int avail_in;        /* number of bytes available at next_in */
        mz_ulong total_in;            /* total number of bytes consumed so far */

        unsigned char *next_out; /* pointer to next byte to write */
        unsigned int avail_out;  /* number of bytes that can be written to next_out */
        mz_ulong total_out;      /* total number of bytes produced so far */

        char *msg;                       /* error msg (unused) */
        struct mz_internal_state *state; /* internal state, allocated by zalloc/zfree */

        mz_alloc_func zalloc; /* optional heap allocation function (defaults to malloc) */
        mz_free_func zfree;   /* optional heap free function (defaults to free) */
        void *opaque;         /* heap alloc function user pointer */

        int data_type;     /* data_type (unused) */
        mz_ulong adler;    /* adler32 of the source or uncompressed data */
        mz_ulong reserved; /* not used */
    } mz_stream;

    typedef mz_stream *mz_streamp;

mz_ulong mz_crc32(mz_ulong crc, const unsigned char* data, size_t size);
mz_ulong mz_adler32(mz_ulong adler, const unsigned char* data, size_t size);
int mz_inflateInit(mz_streamp stream);
int mz_inflateInit2(mz_streamp stream, int window_bits);
// The firmware and pinned SDK use NO_FLUSH with window bits 15 or -15.
int mz_inflate(mz_streamp stream, int flush);
// Raw streams may omit Adler computation; keep this choice fixed per stream.
// Zlib-wrapped streams always compute and verify Adler.
int mz_inflate_ex(mz_streamp stream, int flush, int compute_adler32);
int mz_inflateEnd(mz_streamp stream);

#ifdef __cplusplus
}
#endif
