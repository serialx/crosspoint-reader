"""Replace PNGdec 1.1.6's private zlib with the bounded PngZlib miniz adapter."""

import argparse
import re
from pathlib import Path
import sys

ZLIB_SOURCES = (
    "adler32.c", "crc32.c", "infback.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c",
)
HEADER_OLD = '#include "zutil.h"\n#include "inftrees.h"\n#include "inflate.h"'
HEADER_NEW = '// Preserve the compiler setting previously supplied by zconf.h.\n#ifdef __GNUC__\n#pragma GCC optimize("O2")\n#endif\n#include <PngZlib.h>'

ARENA_OLD = 'uint8_t ucZLIB[32768 + sizeof(struct inflate_state)]; // put this here to avoid needing malloc/free'
ARENA_NEW = 'PngZlibArena ucZLIB; // owned by this decoder; no separate inflate allocation'
INIT_OLD = '''    // The allocation functions are disabled and zlib has been modified
    // to not use malloc/free and instead the buffer is part of the PNG class
    d_stream.zalloc = (alloc_func)0;
    d_stream.zfree = (free_func)0;
    d_stream.opaque = (voidpf)0;
    // Insert the memory pointer here to avoid having to use malloc() inside zlib
    state = (struct inflate_state FAR *)pPage->ucZLIB;
    d_stream.state = (struct internal_state FAR *)state;
    state->window = &pPage->ucZLIB[sizeof(struct inflate_state)]; // point to 32k dictionary buffer
    err = inflateInit(&d_stream);
#ifdef FUTURE
//    if (inpage->cCompression == PIL_COMP_IPHONE_FLATE)
//        err = mz_inflateInit2(&d_stream, -15); // undocumented option which ignores header and crcs
//    else
//        err = mz_inflateInit2(&d_stream, 15);
#endif // FUTURE'''
INIT_NEW = '''    err = pngZlibInit(&d_stream, &pPage->ucZLIB);
    if (err != MZ_OK) {
        pPage->iError = PNG_MEM_ERROR;
        return pPage->iError;
    }'''


def replace_once(text, before, after):
    if after in text:
        return text
    if text.count(before) != 1:
        raise RuntimeError("PNGdec patch does not match: " + before[:80])
    return text.replace(before, after, 1)


def patch_library(library):
    source = Path(library) / "src"
    header_path, decoder_path = source / "PNGdec.h", source / "png.inl"
    header = header_path.read_text()
    decoder = decoder_path.read_text()
    header = replace_once(header, HEADER_OLD, HEADER_NEW)
    header = replace_once(header, ARENA_OLD, ARENA_NEW)
    decoder = replace_once(decoder, '#include "zlib.h"', '#include <PngZlib.h>')
    decoder = replace_once(decoder, INIT_OLD, INIT_NEW)
    decoder = replace_once(decoder, '    struct inflate_state *state;', '    // Inflate storage belongs to pPage.')
    decoder = replace_once(decoder, 'z_stream d_stream;', 'PngZlibStream d_stream;')
    decoder = replace_once(decoder, 'crc32(0, &s[12], len+4)', 'mz_crc32(0, &s[12], len+4)')
    decoder = replace_once(decoder, 'inflate(&d_stream, Z_NO_FLUSH,', 'pngZlibInflate(&d_stream, MZ_NO_FLUSH,')
    decoder = replace_once(decoder, 'inflateEnd(&d_stream)', 'pngZlibEnd(&d_stream)')
    for field in ('avail_in', 'avail_out', 'next_in', 'next_out'):
        decoder = decoder.replace('d_stream.' + field, 'd_stream.stream.' + field)
    for status in ('OK', 'STREAM_END', 'DATA_ERROR', 'STREAM_ERROR', 'BUF_ERROR'):
        # The boundary avoids rewriting MZ_* on subsequent runs.
        decoder = re.sub(r'\bZ_' + status + r'\b', 'MZ_' + status, decoder)
    for name in ZLIB_SOURCES:
        if not (source / name).is_file() and not (source / (name + '.disabled')).is_file():
            raise RuntimeError('PNGdec zlib source missing: ' + name)
    # Validate every edit before changing files. Renaming excludes the private C
    # sources from both PlatformIO and the host oracle's source glob.
    if header != header_path.read_text():
        header_path.write_text(header)
    if decoder != decoder_path.read_text():
        decoder_path.write_text(decoder)
    for name in ZLIB_SOURCES:
        path = source / name
        if path.is_file():
            path.rename(source / (name + '.disabled'))
    return True


def patch_pngdec(env, require_found=False):
    library = Path(env.subst('$PROJECT_LIBDEPS_DIR')) / env.subst('$PIOENV') / 'PNGdec'
    if library.is_dir():
        return patch_library(library)
    if require_found:
        sys.stderr.write('ERROR: PNGdec dependency was not found to patch\n')
        raise SystemExit(1)
    return False


def before_link(source, target, env):
    patch_pngdec(env, require_found=True)


if 'Import' in globals():
    Import('env')  # noqa: F821
    patch_pngdec(env)  # noqa: F821
    env.AddPreAction('$BUILD_DIR/${PROGNAME}.elf', before_link)  # noqa: F821
elif __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library_path', type=Path)
    patch_library(parser.parse_args().library_path)
