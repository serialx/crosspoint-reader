"""Compile the actual shipping error helper with no native text-draw field."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(root / 'test/reader_lifecycle'))
from test_popup_resource import definition
source = (root / 'rust/crates/xp-ffi/src/app/epub/book_render.rs').read_text()
helper = definition(source, '    fn book_error_text(')
code = r'''
#![allow(dead_code)]
#[derive(Default)]struct Rect{x:i16,y:i16,w:i16,h:i16}
#[derive(Default)]struct Command{rect:Rect,font:i32,kind:u8,paint:u8,style:u8}
mod xp_ui {pub mod scene {pub enum Kind{Text}}}
struct Text;impl Text{unsafe fn bytes(&self)->Option<&[u8]>{Some(b"error")}}
struct Resources;
impl Resources{
 unsafe fn translate(&self,id:u16)->Text{assert!([1,2,3].contains(&id));Text}
 unsafe fn text_width(&self,_:Text,font:i32,style:u8)->i32{assert_eq!((font,style),(12,1));88}
}
struct Geometry{width:i32,height:i32,ui_font_12:i32}
struct Session{resources:Resources}
mod text_run {
 pub unsafe fn reader_error(c:&super::Command,r:super::Rect,t:&[u8]){
 assert_eq!((c.rect.x,c.rect.y,c.font,c.style,c.paint),(220,300,12,1,0));
 assert_eq!((r.x,r.y,r.w,r.h),(0,0,528,792));assert_eq!(t,b"error");
 }
}
impl Session{fn book_geometry(&self)->Geometry{Geometry{width:528,height:792,ui_font_12:12}}HELPER}
fn main(){let s=Session{resources:Resources};for id in [1,2,3]{s.book_error_text(id);}}
'''.replace('HELPER',helper).replace('struct Text;', '#[derive(Clone,Copy)]struct Text;')
with tempfile.TemporaryDirectory(prefix='reader-error-dispatch-') as directory:
    path = Path(directory)
    (path / 'main.rs').write_text(code)
    subprocess.run(['rustc', '--edition=2021', '-Dwarnings', str(path / 'main.rs'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
# Keep language fixture wording tied to the three supported translation catalogs.
pixels = (root / 'test/text_golden/ErrorPixels.cpp').read_text()
for language in ['english', 'hebrew', 'arabic']:
    lines = (root / 'lib/I18n/translations' / (language + '.yaml')).read_text().splitlines()
    for name in ['STR_PAGE_LOAD_ERROR', 'STR_EMPTY_CHAPTER', 'STR_OUT_OF_BOUNDS']:
        value = next(line.split(': ', 1)[1] for line in lines if line.startswith(name + ': '))
        assert value in pixels, (language, name)
