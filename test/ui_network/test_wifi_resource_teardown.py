"""Execute the production teardown bodies with observable resource destructors."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
source=(root/'src/activities/network/WifiSelectionActivity.cpp').read_text()
header=(root/'src/activities/network/WifiSelectionActivity.h').read_text()
def body(signature):
    start=source.index(signature)
    opening=source.index('{',start)
    end=opening+1
    depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    return source[opening:end]
resources=re.findall(r'union\s*\{\s*([^;{}]+)\s+(\w+);\s*\};',header)
assert len(resources)==12,resources
names=[name for _,name in resources]
release=body('void WifiSelectionActivity::releaseResources()')
assert re.findall(r'\b(\w+)\.~\w+\(\);',release)==list(reversed(names))
fields=[]
initializers=[]
for original,name in resources:
    kind=('GfxRendererTarget' if name=='textTarget' else 'vector' if 'vector<' in original else 'optional' if 'optional<' in original else 'basic_string')
    fields.append('union { '+kind+' '+name+'; };')
    initializers.append(name+'("'+name+'")')
program=r'''
#include <cassert>
#include <string>
#include <vector>
std::vector<std::string> events;
struct Probe { const char* tag; explicit Probe(const char* s):tag(s){} ~Probe(){events.emplace_back(tag);} };
struct GfxRendererTarget:Probe{using Probe::Probe;};
struct vector:Probe{using Probe::Probe;};
struct basic_string:Probe{using Probe::Probe;};
struct optional:Probe{using Probe::Probe;};
struct Activity{~Activity(){events.emplace_back("Activity");}};
struct Rows{
 bool live=true;
 void reset(){if(live){events.emplace_back("rows");live=false;}}
 ~Rows(){reset();}
};
struct Commands{~Commands(){events.emplace_back("commands");}};
struct WifiSelectionActivity:Activity {
 Commands commands;
 FIELDS
 Rows visibleRows;
 bool resourcesLive=true;
 WifiSelectionActivity():INITIALIZERS{}
 ~WifiSelectionActivity() DESTRUCTOR
 void releaseResources() RELEASE
};
int main(){
 const std::vector<std::string> expected=EXPECTED;
 {WifiSelectionActivity activity;}
 assert(events==expected);
 events.clear();
 {WifiSelectionActivity activity;activity.visibleRows.reset();activity.releaseResources();activity.releaseResources();}
 assert(events==expected);
}
'''.replace('FIELDS','\n'.join(fields)).replace('INITIALIZERS',','.join(initializers)).replace('DESTRUCTOR',body('WifiSelectionActivity::~WifiSelectionActivity()')).replace('RELEASE',release).replace('EXPECTED','{'+','.join('"'+name+'"' for name in ['rows']+list(reversed(names))+['commands','Activity'])+'}')
with tempfile.TemporaryDirectory(prefix='wifi-teardown-') as temporary:
    path=Path(temporary)
    (path/'test.cpp').write_text(program)
    binary=path/'test'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror',str(path/'test.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Production Wi-Fi teardown: row/native/command/Activity order and idempotent release PASS')
