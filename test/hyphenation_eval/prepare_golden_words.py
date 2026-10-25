#!/usr/bin/env python3
"""Select compact C++ goldens from the archived full capture.

Usage: prepare_golden_words.py --full-dir /path/to/m1-goldens-full
Never replace captured C++ outputs with Rust outputs to resolve a failure.
"""
import argparse
import gzip
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SEED = b"crosspoint-hyphenation-v1"
PER_CORPUS = 64
TAGS = "en fr de ru es it pl sv uk fi pt unknown".split()
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--full-dir", type=Path, required=True)
parser.add_argument("--output-dir", type=Path, default=ROOT)
args = parser.parse_args()
full_bytes = (args.full_dir / "golden-words.txt").read_bytes()
full_words = [bytes.fromhex(line) if line != "-" else b""
              for line in full_bytes.decode().splitlines() if not line.startswith("#")]
words = []
for source in sorted((ROOT / "resources").glob("*_hyphenation_tests.txt")):
    corpus = {line.split("|")[0].encode() for line in source.read_text().splitlines()
              if line and not line.startswith("#") and "|" in line}
    def rank(word):
        return (hashlib.sha256(SEED + b"\0" + source.name.encode() + b"\0" + word).digest(), word)
    words.extend(sorted(corpus, key=rank)[:PER_CORPUS])

edges = [
    '', 'a', "all'improvviso", "l'état-major", "don't", 'abc’def',
    'US-Satellitensystems', 'Satel\u00adliten', '(hyphenation[12]).',
    '[12]', 'a[12', 'abc123', '«école»', 'e\u0301le\u0300ve',
    'A\u0308O\u0308U\u0308', '中文中文中文', '한국어한국어한국어',
    'Український', 'suomalainen', 'a' * 68, 'a' * 69, 'a' * 150,
    'a' * 160, 'a' * 200, 'é' * 79, 'é' * 80, 'a' * 200 + '-hyphenation',
]
words.extend(word.encode() for word in edges)
separators = [
    0x2D, 0xAD, 0x58A, *range(0x2010, 0x2016), 0x2043, 0x207B,
    0x208B, 0x2212, 0x2E17, 0x2E3A, 0x2E3B, 0xFE58, 0xFE63,
    0xFF0D, 0x5F, 0x2026,
]
words.extend(('alphabet' + chr(cp) + 'hyphenation').encode() for cp in separators)
malformed = [
    b'\x80', b'\xff', b'\xc0\xaf', b'\xe0\x80\xaf', b'\xed\xa0\x80',
    b'\xf4\x90\x80\x80', b'\xf8\x80\x80\x80\x80',
    b'\xfc\x80\x80\x80\x80\x80', b'\xe2\x82', b'\x00',
]
words.extend(b'ab' + raw + b'cdefgh' for raw in malformed)
words.extend(b'abcdef' + raw for raw in [b'\xc2', b'\xe2\x82', b'\xf0\x90\x80'])

selected = set(words)
assert selected <= set(full_words), "Full capture is missing selected inputs"
indices = {index: compact for compact, index in enumerate(
    index for index, word in enumerate(full_words) if word in selected)}
header = (f"# SHA256-ranked first {PER_CORPUS} unique words per corpus; seed={SEED.decode()}\n"
          "# Plus all handwritten edges; order follows the full C++ capture.\n"
          f"# Full words SHA256: {hashlib.sha256(full_bytes).hexdigest()}\n")
args.output_dir.mkdir(parents=True, exist_ok=True)
(args.output_dir / "golden-words.txt").write_text(
    header + "".join((full_words[index].hex() or "-") + "\n" for index in indices))
count = 0
with gzip.open(args.full_dir / "golden-breaks.txt.gz", "rt") as source, \
        (args.output_dir / "golden-breaks.txt").open("w") as target:
    target.write(header + "# One tag per language behavior, both fallback flags; indices into golden-words.txt.\n")
    for line in source:
        if line.startswith("#"):
            continue
        tag, fallback, index, *breaks = line.split()
        if tag in TAGS and int(index) in indices:
            target.write(" ".join([tag, fallback, str(indices[int(index)]), *breaks]) + "\n")
            count += 1
assert count == len(indices) * len(TAGS) * 2
print(f"{len(indices)} words; {count} cases")
