#!/usr/bin/env python3
"""Capture the unchanged C++ orphan/widow and heading carry decisions."""
import argparse
import hashlib
import random
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit('refusing to overwrite captured oracle')
    source = (ROOT / 'lib/Epub/Epub/parsers/ChapterPageAssembler.cpp').read_text()
    body = source[source.index('size_t ChapterPageAssembler::linesToCarry'):source.index('ChapterPageAssembler::~')]
    body = body.replace('ChapterPageAssembler::', 'Probe::')
    cpp = r'''
#include <cstdio>
#include <cstddef>
#include <vector>
#include <memory>
constexpr int TAG_PageLine = 1;
struct Element { int tag; int getTag() const { return tag; } };
struct Page { std::vector<std::unique_ptr<Element>> elements; };
struct Paragraph { size_t after; size_t linesAfterCurrentLine() const { return after; } };
struct Probe {
  static constexpr size_t MAX_CARRIED_LINES = 6;
  Paragraph* layoutParagraph;
  bool layoutParagraphHasDropCap;
  size_t paragraphLinesOnPage, keepWithNextLines, recentLineCount;
  Page* currentPage;
  size_t linesToCarry(size_t& paragraphLines) const;
  size_t keepWithNextCarry() const;
  bool canCarry(size_t carry) const;
};
''' + body + r'''
int main() {
  unsigned active, cap, paragraph, keep, after, recent, count, trailing;
  while (scanf("%u%u%u%u%u%u%u%u", &active,&cap,&paragraph,&keep,&after,&recent,&count,&trailing) == 8) {
    Paragraph p{after}; Page page;
    for (unsigned i=0; i<count; ++i) page.elements.push_back(std::make_unique<Element>(Element{i+trailing>=count ? TAG_PageLine : 3}));
    Probe probe{active ? &p : nullptr, cap != 0, paragraph, keep, recent, &page};
    size_t carriedParagraph=0; const size_t carry=probe.linesToCarry(carriedParagraph);
    printf("%zu %zu %zu\n", carry,carriedParagraph,probe.keepWithNextCarry());
  }
}
'''
    rng = random.Random(710)
    cases = []
    for _ in range(2048):
        count = rng.randrange(10)
        values = [rng.randrange(2), rng.randrange(2), rng.randrange(8), rng.randrange(7), rng.randrange(3), rng.randrange(7), count, rng.randrange(count+1)]
        cases.append(' '.join(map(str, values)))
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp)/'capture.cpp', Path(tmp)/'capture'
        src.write_text(cpp)
        subprocess.run(['c++','-std=c++20',str(src),'-o',str(exe)],check=True)
        answers = subprocess.run([str(exe)],input='\n'.join(cases)+'\n',text=True,capture_output=True,check=True).stdout.splitlines()
    assert len(answers) == len(cases)
    revision = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    header = f'# C++ carry decisions at {revision}; source sha256={hashlib.sha256(source.encode()).hexdigest()}\n'
    args.output.write_text(header + '# active dropcap paragraph_lines keep_lines lines_after recent count trailing => carry paragraph_carry heading_carry\n' + ''.join(f'{case} => {answer}\n' for case,answer in zip(cases,answers)))
    print(f'{len(cases)} captured cases: {args.output}')


if __name__ == '__main__':
    main()
