"""Fail the replacement after real Rust scene overflow and verify republication."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def method(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include "RustActivityAdapter.h"
#include <xp_ui_reader_chrome.h>
#include <algorithm>
#include <cassert>
#include <memory>
static bool fail=false;
static unsigned attempts=0;
template<class T> std::unique_ptr<T> makeUniqueNoThrow(size_t n){++attempts;return fail?nullptr:std::make_unique<T>(n);}
'''
code += method('src/activities/reader/ReaderChromeListActivity.h', 'class ReaderChromeCommands') + ';\n'
for sig in ['bool ReaderChromeCommands::reserve(', 'bool ReaderChromeCommands::grow(']:
    code += method('src/activities/reader/ReaderChromeListActivity.cpp', sig)
code += r'''
void sampleReaderChromeTheme(const GfxRenderer&,XpUiReaderTheme& t,bool){
 t.screen=t.safe={0,0,480,800};t.row_min=48;t.row_padding=8;t.scroll_width=3;
 t.top_padding=10;t.header_height=40;t.spacing=8;t.body_font=1;t.small_font=0;t.title_font=2;
}
class ReaderChromeListActivity:public RustActivityAdapter {
 public:
 using RustActivityAdapter::RustActivityAdapter;
 struct {ReaderChromeCommands commands;XpUiReaderTheme theme{};XpUiReaderListState next{};XpUiReaderHitLayout nextHits{};} arena;
 XpUiReaderListState state{};
 XpUiReaderHitLayout publishedHits{};
 int renderingScroll=0;
 uint8_t kind=XP_UI_READER_BOOKMARKS;
 Publication publication;
 Publication* rustPublication()override{return &publication;}
 int listCount()const{return 20;}
 XpUiReaderRows rows(){return {
 nullptr,[](void*,uint16_t,uint16_t)->uint8_t{return 1;},
 [](void*,uint16_t i,XpUiReaderRow* r)->uint8_t{*r={uint32_t(i)*4+1,0,0,0,0,0,{0,0}};return 1;},
 [](void*,uint32_t,int32_t,uint8_t)->int32_t{return 40;},
 [](void*,uint32_t,int32_t,uint8_t,uint8_t,int16_t)->int32_t{return 20;},
 [](void*,int32_t)->int32_t{return 20;},20,32,1};}
 bool buildRustScene(XpUiScene&)override;
 void rustPublished()override;
 bool renderList(bool refresh=true);
};
'''
for sig in ['bool ReaderChromeListActivity::buildRustScene(', 'void ReaderChromeListActivity::rustPublished(', 'bool ReaderChromeListActivity::renderList(']:
    code += method('src/activities/reader/ReaderChromeListActivity.cpp', sig)
code += r'''
int main(){
 for(bool first:{false,true}){
  GfxRenderer renderer;renderer.finishRefresh=true;MappedInputManager input;
  ReaderChromeListActivity list("commands",renderer,input);xp_ui_reader_list_init(&list.state,0);
  assert(list.arena.commands.reserve(1));
  if(first){assert(list.renderList());assert(list.publication.ready);list.arena.commands.reset();assert(list.arena.commands.reserve(1));}
  const auto before=attempts;fail=true;
  assert(!list.renderList());
  assert(attempts==before+1&&!list.arena.commands.ready()&&!list.publication.ready);
  fail=false;assert(list.renderList());
  assert(list.arena.commands.ready()&&list.publication.ready&&list.state.list.visible>0);
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    out=Path(directory)
    for name in ['RustActivityAdapter.h','RustActivityAdapter.cpp']:
        (out/name).write_bytes((ROOT/'src/activities'/name).read_bytes())
    (out/'commands.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-pthread','-UNDEBUG','-I'+str(out),'-I'+str(ROOT/'test/ui_runtime/stubs'),
                    '-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(out/'commands.cpp'),str(out/'RustActivityAdapter.cpp'),
                    str(ROOT/'test/cmake/xp_ffi_host.cpp'),sys.argv[1],'-o',str(out/'commands')],check=True)
    subprocess.run([str(out/'commands')],check=True,timeout=30)
print('Real view overflow, exact replacement failure and republication pass')
