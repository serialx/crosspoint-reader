#!/usr/bin/env python3
"""Pin the Rust pressure order and protection eviction set to native registration."""
from pathlib import Path
import argparse, re, subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);a=p.parse_args();a.build.mkdir(parents=True,exist_ok=True)
cpp=(ROOT/'lib/GfxRenderer/GfxRenderer.cpp').read_text()
registered=re.findall(r'registerSink\(\{"([^"]+)",\s*(\d+),.*?xp_font_cache_release\(fontMap.raw\(\),\s*(XP_FONT_EVICT_\w+)',cpp,re.S)
assert len(registered)==3,registered
header=(ROOT/'rust/crates/xp-ffi/include/xp_font_registry.h').read_text()
enums=re.search(r'enum\s*\{([^}]*XP_FONT_EVICT_VECTOR[^}]*)\}',header,re.S).group(1)
values={name:i for i,name in enumerate(re.findall(r'XP_FONT_\w+',enums))}
port=(ROOT/'rust/crates/xp-ffi/src/app/epub/book_owner/protection.rs').read_text()
operations={name:int(operation) for name,operation in re.findall(r'fn (render|sd|vector)\(&mut self\).*?font_runtime::release\(self.0.cast\(\), (\d+)\)',port,re.S)}
assert len(operations)==3,operations
expected=[values[name] for _,_,name in sorted(registered,key=lambda x:int(x[1]))]
assert [operations[name] for name in ('render','sd','vector')]==expected,(operations,registered,values)
assert '#if CROSSPOINT_VECTOR_FONTS' in cpp[:cpp.index('"gfx.ttfGlyphArenas"')].split('"gfx.sdFontMini"')[-1]
assert '#[cfg(feature = "vector-fonts")]' in port[port.index('fn vector'):port.index('fn reclaim')]
main=(ROOT/'src/main.cpp').read_text()
assert main.index('renderer.setFontCacheManager(&fontCacheManager)') < main.index('setupDisplayAndFonts(resume != BootResume::Splash)') < main.index('activityManager.goToReader(')
for base in ('lib','src'):
 for path in (ROOT/base).rglob('*.cpp'):
  if path.name!='GfxRenderer.cpp': assert '.registerSink(' not in path.read_text(),path
module=ROOT/'rust/crates/xp-core/src/font/pressure.rs'
source=f'''#[path = "{module}"] mod pressure;
use pressure::Pressure;
#[derive(Default)] struct Fake {{calls: Vec<u8>, have: usize, freed: [usize;3]}}
impl Pressure for Fake {{
 fn free_default(&mut self)->usize{{self.have}}
 fn render(&mut self)->usize{{self.calls.push({expected[0]});self.freed[0]}}
 fn sd(&mut self)->usize{{self.calls.push({expected[1]});self.freed[1]}}
 fn vector(&mut self)->usize{{self.calls.push({expected[2]});self.freed[2]}}
}}
fn main() {{
 for first in [0,16384,usize::MAX/4] {{for second in [0,16384] {{
 let mut f=Fake{{have:100000,freed:[first,second,0],..Fake::default()}};
 pressure::clear_all(&mut f);assert_eq!(f.calls,vec!{expected});
 }}}}
 for stop in 0..4 {{let mut f=Fake::default();if stop==0{{f.have=16384}}else{{f.freed[stop-1]=16384;}}
 pressure::reclaim(&mut f);assert_eq!(f.calls,vec!{expected}[..stop]);}}
}}
'''
(a.build/'pressure.rs').write_text(source)
subprocess.run(['rustc','--edition=2021',str(a.build/'pressure.rs'),'-o',str(a.build/'pressure')],check=True)
subprocess.run([str(a.build/'pressure')],check=True)
print('Rust pressure set/order matches all native sinks and production initialization')
