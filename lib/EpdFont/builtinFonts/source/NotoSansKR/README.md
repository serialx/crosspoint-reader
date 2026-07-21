# Noto Sans KR (KS X 1001 cut)

These fonts are a subset of Noto Sans KR, cut down to the KS X 1001 modern
Hangul repertoire so the UI fonts can render Korean book titles, filenames,
and menus without shipping the full 11,172-syllable block:

- 2,350 modern Hangul syllables (KS X 1001, i.e. EUC-KR rows 0xB0-0xC8)
- 51 modern compatibility jamo (U+3131-U+3163)

Source: https://github.com/notofonts/noto-cjk `Sans/SubsetOTF/KR/NotoSansKR-{Regular,Bold}.otf`
License: SIL Open Font License 1.1 (see OFL.txt)

## Regenerating the cut

```bash
pip install fonttools

# KS X 1001 syllables are exactly the EUC-KR double-byte rows 0xB0-0xC8.
python3 - <<'EOF'
cps = [ord(bytes([lead, trail]).decode('euc_kr'))
       for lead in range(0xB0, 0xC9) for trail in range(0xA1, 0xFF)]
cps += range(0x3131, 0x3164)  # modern compatibility jamo
with open('unicodes.txt', 'w') as f:
    f.write('\n'.join(f'U+{c:04X}' for c in sorted(cps)))
EOF

pyftsubset NotoSansKR-Regular.otf --unicodes-file=unicodes.txt --output-file=NotoSansKR-Regular.otf
pyftsubset NotoSansKR-Bold.otf    --unicodes-file=unicodes.txt --output-file=NotoSansKR-Bold.otf
```

`fontconvert.py` prunes requested Unicode intervals to the glyphs actually
present in the font stack, so `convert-builtin-fonts.sh` can simply request
the full Hangul Syllables block (0xAC00-0xD7A3) against this cut.
