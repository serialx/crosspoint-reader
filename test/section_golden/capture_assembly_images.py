#!/usr/bin/env python3
"""Capture unchanged C++ image sizing before the assembly port (host-only)."""
import argparse
import hashlib
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--revision', default='b7fa3580', help='C++ baseline revision')
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit('refusing to overwrite captured oracle')
    source = subprocess.check_output(['git', 'show', args.revision + ':lib/Epub/Epub/parsers/ChapterPageAssembler.cpp'], cwd=ROOT, text=True)
    start = source.index('          if (hasCssHeight && hasCssWidth && dims.width > 0')
    end = source.index('          // Flush any pending text block', start)
    body = source[start:end]
    cpp = r'''
#include <cstdio>
#include <cstdint>
#include "Epub/css/CssStyle.h"
#define LOG_DBG(...) ((void)0)
struct View { uint16_t viewportHeight; };
int main() {
  unsigned w, h, vw, vh, cw, flags, wu, hu;
  float em, wv, hv;
  while (scanf("%u%u%u%u%u%f%u%f%u%f%u", &w,&h,&vw,&vh,&cw,&em,&flags,&wv,&wu,&hv,&hu) == 11) {
    struct { unsigned width, height; } dims{w,h};
    View viewport{static_cast<uint16_t>(vh)}; auto* self = &viewport;
    const bool hasCssWidth = flags & 1, hasCssHeight = flags & 2;
    CssStyle imgStyle;
    imgStyle.imageWidth = {wv, static_cast<CssUnit>(wu)};
    imgStyle.imageHeight = {hv, static_cast<CssUnit>(hu)};
    const float emSize = em;
    int containerWidth = cw, displayWidth = 0, displayHeight = 0;
''' + body + r'''
    printf("%d %d\n", displayWidth, displayHeight);
  }
}
'''
    cases = []
    for w,h in [(0,0),(1,65535),(65535,1),(101,99),(1024,768),(481,721),(17,23)]:
        for vw,vh,cw in [(480,720,480),(240,320,180),(640,400,640)]:
            for flags in range(4):
                for unit in range(5):
                    cases.append(f'{w} {h} {vw} {vh} {cw} 15 {flags} 37.5 {unit} 123.25 {unit}')
    data = '\n'.join(cases) + '\n'
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp)/'capture.cpp', Path(tmp)/'capture'
        src.write_text(cpp)
        subprocess.run(['c++','-std=c++20','-I'+str(ROOT/'lib/Epub'),str(src),'-o',str(exe)],check=True)
        answers = subprocess.run([str(exe)],input=data,text=True,capture_output=True,check=True).stdout.splitlines()
    assert len(answers) == len(cases)
    revision = subprocess.check_output(['git','rev-parse',args.revision],cwd=ROOT,text=True).strip()
    header = f'# C++ image sizing at {revision}; source sha256={hashlib.sha256(source.encode()).hexdigest()}\n'
    args.output.write_text(header + '# w h viewport_w viewport_h container_w em flags width unit height unit => display_w display_h\n' + ''.join(f'{case} => {answer}\n' for case,answer in zip(cases,answers)))
    print(f'{len(cases)} captured cases: {args.output}')


if __name__ == '__main__':
    main()
