#!/usr/bin/env python3
"""Paired nothrow-allocation fault traces for original and Rust-backed book facades."""
import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser()
p.add_argument('archive')
p.add_argument('report')
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
archive=Path(a.archive).resolve()
rows=[]
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp)
    logging=tmp/'logging';logging.mkdir()
    (logging/'Logging.h').write_text('#pragma once\n#include <cstdio>\n'+''.join(
        f'#define LOG_{level}(tag,fmt,...) std::fprintf(stderr,"{level} [%s] " fmt "\\n",tag __VA_OPT__(,) __VA_ARGS__)\n'
        for level in ['DBG','INF','ERR']))
    original=tmp/'base'
    files=['lib/Txt/TxtToHtml.cpp','lib/Txt/TxtToHtml.h','lib/Xtc/Xtc.cpp','lib/Xtc/Xtc.h',
           'lib/Xtc/Xtc/XtcParser.cpp','lib/Xtc/Xtc/XtcParser.h','lib/Xtc/Xtc/XtcTypes.h',
           'lib/Utf8/Utf8.cpp']
    for file in files:
        path=original/file;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(subprocess.check_output(['git','show',f'0b70268b:{file}'],cwd=root))
    (original/'lib/Memory').symlink_to(root/'lib/Memory',target_is_directory=True)
    subprocess.run(['python3',str(root/'test/xtc_diff/generate.py'),str(tmp/'fixtures')],check=True,stdout=subprocess.DEVNULL)
    for name in ['txt','xtc']:
        exes=[]
        for variant,source in [('base',original),('candidate',root)]:
            exe=tmp/f'{name}-{variant}'
            common=['c++','-std=c++20','-O2','-fno-assume-sane-operator-new','-I'+str(logging),'-I'+str(root/'rust/crates/xp-ffi/include')]
            if name=='txt':
                includes=[source/'lib/Txt',root/'lib/Memory',root/'lib/FsHelpers',root/'lib/Utf8',
                          root/'test/fs_helpers/stubs',root/'test/chapter_html_slim_parser/stubs']
                sources=[root/'test/txt_to_html/StreamOracle.cpp',source/'lib/Txt/TxtToHtml.cpp',
                         source/'lib/Utf8/Utf8.cpp',root/'lib/FsHelpers/FsHelpers.cpp']
            else:
                includes=[root/'test/book_io/stubs',root/'test/xtc_diff/stubs',source/'lib/Xtc',root/'lib/GfxRenderer']
                sources=[root/'test/book_io/XtcOracle.cpp',source/'lib/Xtc/Xtc.cpp',source/'lib/Xtc/Xtc/XtcParser.cpp',
                         root/'lib/GfxRenderer/BitmapHelpers.cpp']
            command=common+['-I'+str(path) for path in includes]+[str(path) for path in sources]
            command += [str(root/'test/assets_oracle/Allocation.cpp'),str(archive),'-o',str(exe)]
            subprocess.run(command,check=True)
            exes.append(exe)
        fixtures=[None] if name=='txt' else [tmp/'fixtures'/f'synthetic-{depth}-16-17-0.xtc' for depth in (1,2)]
        if name=='xtc' and not fixtures: raise SystemExit('Missing XTC resource fixture')
        for fixture in fixtures:
            def run(exe,fail):
                env=dict(os.environ,ASSET_RESOURCES='1')
                if fail is not None: env['ASSET_FAIL_ALLOC']=str(fail)
                return subprocess.run([str(exe)]+([str(fixture)] if fixture else []),env=env,capture_output=True,check=True)
            baseline=run(exes[0],None)
            attempts=int(baseline.stderr.split(b'ALLOC ')[-1].split()[0])
            for fail in [None,*range(attempts)]:
                before=baseline if fail is None else run(exes[0],fail)
                after=run(exes[1],fail)
                before_bytes=[int(n) for n in re.search(rb'peak=(\d+) largest=(\d+) retained=(\d+)',before.stderr).groups()]
                after_bytes=[int(n) for n in re.search(rb'peak=(\d+) largest=(\d+) retained=(\d+)',after.stderr).groups()]
                if before.stdout!=after.stdout or before.stderr.split(b'RESOURCE ')[0]!=after.stderr.split(b'RESOURCE ')[0] or after_bytes[2]!=0 or any(b>a for a,b in zip(before_bytes[:2],after_bytes[:2])):
                    raise SystemExit(f'Allocation trace mismatch {name} {fixture} failure={fail}: {before.stderr!r} / {after.stderr!r}')
                rows.append({'case':name,'fixture':fixture.name if fixture else None,'fail':fail,
                             'base_resources':before.stderr.decode().split('RESOURCE ')[-1].strip(),'resources':after.stderr.decode().split('RESOURCE ')[-1].strip(),'log_bytes':len(after.stderr.split(b'RESOURCE ')[0]),'exact_output_bytes':len(after.stdout)})
Path(a.report).write_text(json.dumps(rows,indent=2)+'\n')
print(f'Paired allocation oracle: {len(rows)} runs, all outputs identical, no peak increase or retained bytes; allocation attempts recorded')
