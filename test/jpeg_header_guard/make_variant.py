"""Generate one host copy of the pinned JPEGDEC source for the header-guard test.

usage: make_variant.py PRISTINE_SRC PATCH_DIR GUARD_PATCH OUT_DIR VARIANT

  guarded           all firmware patches (the shipped decoder)
  guarded_observed  all patches plus observer hooks
  control_observed  all patches except GUARD_PATCH, plus observer hooks
  mutant_<hunk>     guarded_observed with one guard hunk disabled (evidence only)

Each observer reports when the decoder reaches an unsafe use of a file field. The hook is placed
after the guard that should prevent it, so a guarded copy must never report.
"""
import shutil
import subprocess
import sys
from pathlib import Path

PARSED = ('(usMarker == 0xffc0 || usMarker == 0xffc2 || usMarker == 0xffc4 || usMarker == 0xffdb || '
          'usMarker == 0xffdd || usMarker == 0xffda)')

# (anchor line, code inserted before it[, 'all']). An anchor must occur exactly once in both copies, or with 'all'
# at least once (the duplicates sit in JPEGDEC's uncompiled `#ifdef FUTURE` table builder).
HOOKS = [
    ('usMarker = MOTOSHORT(&s[iOffset]);',
     'JHG_OBSERVE("marker_unread", iOffset + 4 > iBytesRead);'),
    ('switch (usMarker)',
     'JHG_OBSERVE("segment_short", usLen < 2);\n'
     'JHG_OBSERVE("segment_past_eof", (long long)iFilePos - iBytesRead + iOffset + usLen > pPage->JPEGFile.iSize);\n'
     f'JHG_OBSERVE("segment_past_read", {PARSED} && iOffset + usLen > iBytesRead);\n'
     'jhg_limit = iOffset + usLen < iBytesRead ? iOffset + usLen : iBytesRead;'),
    ('pPage->JPCI[i].component_id = s[iOffset++];',
     'JHG_OBSERVE("jpci_overflow", i >= MAX_COMPS_IN_SCAN);'),
    ('if (pPage->ucNumComponents == 1) {',
     'JHG_OBSERVE("sof_count", pPage->ucNumComponents < 1 || pPage->ucNumComponents > MAX_COMPS_IN_SCAN);'),
    ('usLen -= 8;', 'JHG_OBSERVE("sof_length_underflow", usLen < 8);'),
    ('usLen -= 3;', 'JHG_OBSERVE("sof_length_underflow", usLen < 3);'),
    ('if (ucTable & 0x10) // convert AC offset of 0x10 into offset of 4',
     'JHG_OBSERVE("dht_table_id", (ucTable & 0xee) != 0);'),
    ('pucShort = &pJPEG->ucHuffDC[iTable*DC_TABLE_SIZE];',
     'JHG_OBSERVE("huff_dc_table", iTable >= 2);'),
    ('ucCode = *p++;  // get actual huffman code', 'JHG_OBSERVE("huff_overflow", cc >= (1 << iBitNum));', 'all'),
    ('code = *p++;  // get actual huffman code', 'JHG_OBSERVE("huff_overflow", cc >= (1 << iBitNum));', 'all'),
    ('pJPEG->JPCI[j].dc_tbl_no = c >> 4;',
     'JHG_OBSERVE("sos_table_selector", (c >> 4) > 1 || (c & 0xf) > 1);'),
    ('iTableOffset = (ucTable & 0xf) * DCTSIZE;',
     'JHG_OBSERVE("dqt_underflow", usLen < ((ucTable & 0xf0) ? DCTSIZE*2+1 : DCTSIZE+1));'),
    ('iTagCount = TIFFSHORT(&cBuf[iOffset], bMotorola);  /* Number of tags in this dir */',
     'JHG_OBSERVE("exif_outside", iOffset < 0 || iOffset + 2 > jhg_limit);'),
    ('unsigned char *p = &cBuf[iOffset + (i*12) +2];',
     'JHG_OBSERVE("exif_outside", (long long)iOffset + i*12 + 14 > jhg_limit);'),
    ('IFD = TIFFLONG(&s[iOffset+12], bMotorola);',
     'JHG_OBSERVE("exif_outside", iOffset + 18 > jhg_limit);'),
    ('IFD = TIFFLONG(&s[IFD + iOffset + 8], bMotorola);',
     'JHG_OBSERVE("exif_outside", (long long)IFD + iOffset + 8 < 0 || (long long)IFD + iOffset + 12 > jhg_limit);'),
    ('if (!JPEGMakeHuffTables(pPage, 0)) //int bThumbnail) DEBUG',
     'JHG_OBSERVE("sos_ignored", jhg_sos != 0);'),
]

# One guard hunk disabled per mutant: (old text, new text), each matched exactly once.
MUTANTS = {
    'dht_id': [('if ((ucTable & 0xee) != 0)', 'if (0)')],
    'dht_id_0_3': [('if ((ucTable & 0xee) != 0)', 'if ((ucTable & 0xef) > 3)')],
    'huff_codes': [('if (iCodes > (2 << i))', 'if (0)')],
    'sos_ids': [('if ((c & 0xf) > 1 || (c & 0xf0) > 0x10)', 'if ((c & 0xf) > 3 || (c & 0xf0) > 0x30)')],
    'tiff_bounds': [('if (iOffset > iEnd - 2)', 'if (0)'),
                    ('if (iTagCount * 12 > iEnd - iOffset - 2)', 'if (0)')],
    'marker_read': [('if (iOffset > iBytesRead - 4)', 'if (0)')],
    'segment_length': [('if (usLen < 2 || usLen > pPage->JPEGFile.iSize - (iFilePos - iBytesRead + iOffset))',
                        'if (0)')],
    'segment_fit': [('usMarker == 0xffdd || usMarker == 0xffda) && usLen > iBytesRead - iOffset)',
                     'usMarker == 0xffdd || usMarker == 0xffda) && 0)')],
    'exif': [('int iEnd = (usLen < iBytesRead - iOffset) ? iOffset + usLen : iBytesRead;',
              'int iEnd = 0x7fff0000;')],
    'sof': [('if (usLen < 8 || s[iOffset+7] < 1 || s[iOffset+7] > MAX_COMPS_IN_SCAN || usLen < 8 + 3 * s[iOffset+7])',
             'if (0)')],
    'dqt': [(' || usLen < ((ucTable & 0xf0) ? DCTSIZE*2+1 : DCTSIZE+1))', ')')],
    'sos': [('if (JPEGGetSOS(pPage, &iOffset)) // get', 'if (JPEGGetSOS(pPage, &iOffset) && 0) // get')],
    'tifflong': [('(uint32_t)*p * 0x1000000', '*p * 0x1000000'), ('(uint32_t)*(p+3) * 0x1000000', '*(p+3) * 0x1000000')],
}


def replace_once(text, old, new, what):
    if text.count(old) != 1:
        sys.exit(f'{what}: expected one match of {old!r}, found {text.count(old)}')
    return text.replace(old, new)


def observe(text):
    lines = text.split('\n')
    for anchor, code, *every in HOOKS:
        hits = [i for i, line in enumerate(lines) if line.strip() == anchor]
        if len(hits) != 1 and not (every and hits):
            sys.exit(f'hook anchor {anchor!r}: {len(hits)} matches')
        for at in reversed(hits):
            indent = lines[at][:len(lines[at]) - len(lines[at].lstrip())]
            lines[at:at] = [indent + c for c in code.split('\n')]
    text = '\n'.join(lines)
    text = replace_once(text, 'static int JPEGGetSOS(JPEGIMAGE *pJPEG, int *iOff)\n',
                        'static int JPEGGetSOS_unobserved(JPEGIMAGE *pJPEG, int *iOff)\n', 'SOS wrapper')
    text = replace_once(text, '} /* JPEGGetSOS() */\n',
                        '} /* JPEGGetSOS() */\nstatic int JPEGGetSOS(JPEGIMAGE *pJPEG, int *iOff)\n'
                        '{ jhg_sos = JPEGGetSOS_unobserved(pJPEG, iOff); return jhg_sos; }\n', 'SOS wrapper')
    return '#include "observer.h"\n' + text


def main():
    pristine, patch_dir, guard_patch, out, variant = sys.argv[1:]
    out = Path(out)
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(Path(pristine) / 'src', out / 'src')
    subprocess.run(['git', 'init', '-q'], cwd=out, check=True)
    patches = sorted(Path(patch_dir).glob('*.patch'))
    if guard_patch not in [p.name for p in patches]:
        sys.exit(f'{guard_patch} is not in {patch_dir}')
    for patch in patches:
        if variant == 'control_observed' and patch.name == guard_patch:
            continue
        subprocess.run(['git', 'apply', str(patch.resolve())], cwd=out, check=True)
    inl = out / 'src' / 'jpeg.inl'
    text = inl.read_text()
    if variant.startswith('mutant_'):
        for old, new in MUTANTS[variant[len('mutant_'):]]:
            text = replace_once(text, old, new, variant)
    if variant != 'guarded':
        text = observe(text)
    inl.write_text(text)


if __name__ == '__main__':
    main()
