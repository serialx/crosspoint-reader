"""Separate direct menu admission from completion of an actual retained menu result."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(HERE), str(ROOT / 'test')]
from test_toc_more_admission import more_fixture, import_adapter, replace_once
from rust_host import rust_link_flags
PRODUCTION = Path('crates/xp-ffi/src/app/epub/menu.rs')

def digest(data): return hashlib.sha256(data).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,required=True)
    parser.add_argument('--evidence-dir',type=Path,default=os.environ.get('XP_MENU_CONFIRM_EVIDENCE_DIR'))
    args=parser.parse_args()
    adapter=import_adapter('menu_confirm_adapter',HERE/'test_menu_parent.py')
    source=more_fixture(adapter,ROOT).split('static void moreCase(',1)[0]
    source+=(HERE/'menu_confirm_admission_main.inc').read_text()
    production=(ROOT/'rust'/PRODUCTION).read_text()
    pattern=re.compile(r'matches!\(origin, MenuOrigin::Direct\)\s*&& self\.model\(\|state\| state\.scene_gate\.failed\)')
    sites=list(pattern.finditer(production)); assert len(sites)==2
    mutants=[]
    for ordinal,site in enumerate(sites,1):
        mutants.append((f'direct-guard-{ordinal}',production[:site.start()]+'false'+production[site.end():],ordinal,
                        f'MENU_CONFIRM_{"FIRST" if ordinal==1 else "SECOND"}_ADMISSION'))
    # This mutant erroneously applies the direct guard to committed child results.
    mutants.append(('result-retroactively-rejected',production.replace('matches!(origin, MenuOrigin::Direct)', 'true'),0,
                    'MENU_RESULT_COMPLETES'))
    paths=list((ROOT/'rust/crates/xp-ffi/src/app/epub').rglob('*.rs'))
    paths += [Path(__file__), HERE/'menu_confirm_admission_main.inc', HERE/'menu_session_rust.inc',
              HERE/'menu_session_fixture.h', HERE/'menu_parent_oracle.cpp', HERE/'test_menu_parent.py',
              HERE/'test_toc_more_admission.py', HERE/'more_admission_main.inc']
    hashes={str(p):digest(p.read_bytes()) for p in paths}
    with tempfile.TemporaryDirectory(prefix='menu-confirm-admission-') as directory:
        work=Path(directory); evidence=args.evidence_dir or work/'evidence'; evidence.mkdir(parents=True,exist_ok=False)
        copied=work/'source'; shutil.copytree(ROOT/'rust',copied/'rust',ignore=shutil.ignore_patterns('target','__pycache__'))
        for name in ('scripts','lib','src'): (copied/name).symlink_to(ROOT/name,target_is_directory=True)
        target=work/'target'; cpp=work/'fixture.cpp'; cpp.write_text(source);(evidence/'fixture.cpp').write_text(source)
        records=[]; commands=[]
        def run(name,command,**kwargs):
            commands.append(list(map(str,command)));(evidence/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
            result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,**kwargs)
            (evidence/(name+'.stdout')).write_bytes(result.stdout);(evidence/(name+'.stderr')).write_bytes(result.stderr)
            return result
        def build(name,strip):
            command=['cargo','build','--manifest-path',str(copied/'rust/Cargo.toml'),'--target-dir',str(target),
                     '-p','xp-ffi','--release','--locked','--features','app-test'+(',reader-strips' if strip else '')]
            built=run(name+'.cargo',command,cwd=copied/'rust');assert built.returncode==0,(name,'Rust build failed')
            return target/'release/libxp_ffi.a'
        def compile_fixture(name,archive,strip):
            binary=work/name; i18n=adapter.generated_i18n_directory(archive)
            built=run(name+'.compile',['c++','-std=c++20','-UNDEBUG','-O0','-fsanitize=address,undefined',
                '-fno-sanitize-recover=all',f'-DXP_STRIP_RENDER={strip}','-I',str(HERE),'-I',str(i18n),str(cpp),
                *rust_link_flags(archive=str(archive)),'-o',str(binary)])
            assert built.returncode==0,(name,'C++ compile failed',built.stderr.decode(errors='replace'))
            return binary
        for strip in (0,1):
            archive=args.archive if strip==0 else build('baseline-strip',1)
            binary=compile_fixture(f'baseline-{strip}',archive,strip)
            for committed in (0,1):
                for reject in range(2+strip):
                    name=f'baseline-{strip}-{committed}-{reject}'
                    ran=run(name,[str(binary),str(committed),str(reject)])
                    assert ran.returncode==0,(name,ran.returncode,ran.stderr.decode(errors='replace'))
                    records.append({'name':name,'returncode':ran.returncode,'stdout_sha256':digest(ran.stdout)})
        for name,changed,reject,marker in mutants:
            (copied/'rust'/PRODUCTION).write_text(changed)
            archive=build(name,1); binary=compile_fixture(name,archive,1)
            ran=run(name,[str(binary),'1' if reject==0 else '0',str(reject)])
            assert ran.returncode==42 and ran.stderr==f'EXPECTED_ASSERTION: {marker}\n'.encode(),(
                name,'specific semantic failure required',ran.returncode,ran.stderr.decode(errors='replace'))
            records.append({'name':name,'returncode':ran.returncode,'marker':marker,'mutant_sha256':digest(changed.encode())})
        assert all(digest(Path(p).read_bytes())==v for p,v in hashes.items()),'source changed'
        (evidence/'manifest.json').write_text(json.dumps({'records':records,'source_sha256':hashes,
            'limits':'Historical menu fixture hooks remain; tests actual shared menu body and retained child intent. '
                     'Physical deterministic lock injection is not real App delivery or a concurrent race.'},indent=2)+'\n')
        print('PASS: 10 baseline cases across both strip modes; 3 specific semantic mutants')
if __name__=='__main__':main()
