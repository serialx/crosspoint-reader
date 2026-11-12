"""Compile the reader physical leaves against the strict host file fake."""
from pathlib import Path
import re, sys
source, output = map(Path, sys.argv[1:])
s = source.read_text()
names = ['xp_native_page_image_file', 'xp_native_page_image_write',
         'xp_native_page_image_codec_log', 'xp_native_page_image_decode']
parts = ['#include <xp_page_paint.h>\n#include <ImageDecoderFactory.h>\n'
         '#include <ImagePipeline.h>\n#include <GifCommon.h>\n'
         '#include <PngDiagnostics.h>\n#include <HalMemory.h>\n']
for name in names:
    matches = re.findall(r'extern "C" [^\n]+ ' + name + r'\([\s\S]*?^}', s, re.M)
    assert len(matches) == 1, name
    parts.append(matches[0] + '\n')
output.write_text(''.join(parts))
