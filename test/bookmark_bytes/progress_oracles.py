#!/usr/bin/env python3
"""Check reader-owned XPath and diagnostics against unchanged native contracts."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def definition(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def native_logs(build):
    mapper = (ROOT / 'lib/KOReaderSync/ProgressMapper.cpp').read_text()
    resolver = (ROOT / 'lib/KOReaderSync/ChapterXPathResolver.cpp').read_text()
    formats = re.findall(r'LOG_DBG\("KOX", "([^"\n]*)"', resolver)
    select = lambda prefix: next(value for value in formats if value.startswith(prefix))
    save = re.search(r'LOG_DBG\("PM", "(-> Progress:[^"\n]*)"', mapper).group(1)
    source = r'''
#include <xp_ffi.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
static void emit(unsigned severity, const char* origin, const char* format, ...) {
  char buffer[4096]; va_list args; va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
  printf("%u:", severity);
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(origin); *p; ++p) printf("%02x", *p);
  printf(":");
  for (size_t i=0; i<256 && buffer[i]; ++i) printf("%02x", static_cast<unsigned char>(buffer[i]));
  puts("");
}
#define LOG_DBG(origin, format, ...) emit(0, origin, format, ##__VA_ARGS__)
// NATIVE_DIAGNOSTIC
int main() {
  char kind; unsigned id, bits, flag;
  while (std::cin >> kind >> id >> bits >> flag) {
    float ratio; memcpy(&ratio, &bits, sizeof(ratio));
    std::string text = flag == 0 ? "" : flag == 1 ? "div" : flag == 2 ? "한글-ancêtre" : std::string("before\0after",12);
    XpSyncPosition p{}; p.spine=2; p.page=3; p.pages=11; p.offset=17; p.paragraph=4; p.li=2; p.has_li=flag%2;
    memcpy(p.anchor,text.data(),text.size());
    XpSyncMapEvent e{id,4,flag%2 ? 5 : -1,7,8,ratio,31,97,{reinterpret_cast<const uint8_t*>(text.data()),text.size()}};
    if(kind=='M') diagnostic(nullptr,&p,&e);
    if(kind=='S') emit(0,"PM","SAVE_FORMAT",2,3,11,static_cast<double>(ratio*100),text.c_str());
    if(kind=='X') {
      if(id==0 && !text.empty()) emit(0,"KOX","PARAGRAPH_OK",4u,2,text.c_str());
      if(id==0 && text.empty()) emit(0,"KOX","PARAGRAPH_MISSING",4u,2);
      if(id==1 && !text.empty()) emit(0,"KOX","OFFSET_OK",4u,2,text.c_str());
      if(id==1 && text.empty()) emit(0,"KOX","OFFSET_MISSING",4u,2);
      if(id==2 && !text.empty()) emit(0,"KOX","PROGRESS_OK",static_cast<double>(ratio),2,text.c_str());
      if(id==2 && text.empty()) emit(0,"KOX","PROGRESS_MISSING",static_cast<double>(ratio),2);
    }
    if(kind=='E') emit(1,"KOX",flag ? "Final XML parse error: %s" : "XML parse error: %s","syntax error");
    if(kind=='A') emit(1,"KOX","Failed to create XML parser");
  }
}
'''
    source = source.replace('// NATIVE_DIAGNOSTIC', definition(mapper, 'void diagnostic('))
    for token, value in {
        'SAVE_FORMAT': save, 'PARAGRAPH_OK': select('Resolved paragraph'),
        'PARAGRAPH_MISSING': select('Paragraph'), 'OFFSET_OK': select('Resolved visible'),
        'OFFSET_MISSING': select('Visible offset'), 'PROGRESS_OK': select('Resolved progress'),
        'PROGRESS_MISSING': select('Could not resolve progress'),
    }.items():
        source = source.replace(token, value)
    for message in ('Final XML parse error: %s', 'XML parse error: %s', 'Failed to create XML parser'):
        assert '"' + message + '"' in resolver
    path = build / 'progress-logs.cpp'
    path.write_text(source)
    binary = build / 'progress-logs-native'
    subprocess.run(['c++', '-std=c++20', '-O2', '-I', str(ROOT/'rust/crates/xp-ffi/include'), str(path), '-o', str(binary)], check=True)
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    args = parser.parse_args()
    args.build.mkdir(parents=True, exist_ok=True)
    target = args.build / 'rust'
    subprocess.run(['cargo', 'build', '--offline', '--locked', '--manifest-path', str(HERE/'rust/Cargo.toml'),
                    '--target-dir', str(target), '--features', 'app-test', '--bin', 'bookmark-xpath', '--bin', 'bookmark-progress-logs'], check=True)
    xpath = [sys.executable, str(ROOT/'test/koreader_xpath_resolver/oracle.py'), '--oracle', str(target/'debug/bookmark-xpath'), 'verify']
    subprocess.run(xpath, check=True)
    for control in ('offset', 'progress'):
        with (args.build/f'negative-{control}.log').open('wb') as log:
            run = subprocess.run(xpath, env=dict(os.environ, XP_BOOKMARK_XPATH_CONTROL=control), stdout=log, stderr=subprocess.STDOUT)
        assert run.returncode != 0, control
    bits = [0, 0x80000000, 0x3f000000, 0x3eaaaaab, 0x3f7fffff, 0x4b800000,
            0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00000]
    state = 0x7319a
    for _ in range(128):
        state = (state * 1664525 + 1013904223) & 0xffffffff
        bits.append(state)
    wire = ''.join(f'{kind} {identity} {value} {flag}\n'
                   for value in bits for flag in range(4)
                   for kind, identity in [('M', n) for n in range(1,14)] + [('S',0),('X',0),('X',1),('X',2),('E',0),('A',0)])
    reference = subprocess.check_output([str(native_logs(args.build))], input=wire.encode())
    candidate = target/'debug/bookmark-progress-logs'
    actual = subprocess.check_output([str(candidate)], input=wire.encode())
    (args.build/'native-logs.txt').write_bytes(reference)
    (args.build/'rust-logs.txt').write_bytes(actual)
    before, after, inputs = reference.splitlines(), actual.splitlines(), wire.splitlines()
    assert len(before) == len(after) == len(inputs)
    target_nan = []
    for index, (want, got) in enumerate(zip(before, after)):
        if want == got:
            continue
        kind, identity, value, flag = inputs[index].split()
        value = int(value)
        negative_nan = value & 0xff800000 == 0xff800000 and value & 0x7fffff != 0
        # macOS printf drops a NaN sign. The pinned C3 newlib4.3 svfprintf keeps
        # it: __unorddf2 -> high-word sign test -> '-' at 235(sp). Preserve both
        # raw transcripts; this exception changes no firmware or native source.
        prefix, payload = want.rsplit(b':', 1)
        expected = bytes.fromhex(payload.decode()).replace(b'nan', b'-nan', 1)
        if negative_nan and b'-nan' not in bytes.fromhex(payload.decode()) and got == prefix + b':' + expected.hex().encode():
            target_nan.append(index)
            continue
        raise AssertionError(f'diagnostic mismatch {index}: {inputs[index]} {want!r} {got!r}')
    (args.build/'target-signed-nan-cases.txt').write_text(''.join(inputs[i]+'\n' for i in target_nan))
    mutated = subprocess.check_output([str(candidate)], input=wire.encode(), env=dict(os.environ, XP_BOOKMARK_DIAG_CONTROL='severity'))
    assert mutated != actual
    assert mutated.splitlines()[0] != actual.splitlines()[0]
    print(f'{len(before)-len(target_nan)} native host diagnostic messages match; {len(target_nan)} C3 signed-NaN cases match the target formatter contract; XPath offset/progress and diagnostic severity controls rejected')


if __name__ == '__main__':
    main()
