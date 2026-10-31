import pathlib
import sys
source, output = map(pathlib.Path, sys.argv[1:])
s = source.read_text().replace('std::malloc(', 'linealloc::allocate(').replace('std::free(', 'linealloc::release(')
s = s.replace('std::realloc(', 'linealloc::resize(').replace('posix_memalign(', 'linealloc::aligned(')
output.write_text('#include "Allocations.h"\n' + s)
