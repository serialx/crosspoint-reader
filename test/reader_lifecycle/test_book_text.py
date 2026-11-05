"""Actual Rust cache row -> native toolbar adapter -> shipping toolbar/SDK layout."""
from pathlib import Path
import argparse, hashlib, json, signal, subprocess, tempfile
from book_facade_composition import facade_source, compile_command
from test_book_facade import make_fixture
from test_popup_resource import definition, native_leaf, replace_once

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent

def composition(root):
    source=facade_source(root)
    native=(root/'src/activities/reader/RustEpubResources.cpp').read_text()
    ui=(HERE/'book_text_ui.cpp').read_text()
    header=(root/'src/activities/reader/ReaderToolbarUi.h').read_text()
    cpp=(root/'src/activities/reader/ReaderToolbarUi.cpp').read_text()
    commands=(root/'src/activities/reader/ReaderChromeListActivity.cpp').read_text()
    declarations=definition((root/'src/activities/reader/ReaderChromeListActivity.h').read_text(),'class ReaderChromeCommands {')+';\n'
    declarations+=definition(header,'class ReaderToolbarUi {').replace('freeink::ui::GfxRendererTarget','TextTarget')+';\n'
    methods='\n'.join(definition(commands,s) for s in ['bool ReaderChromeCommands::reserve(', 'bool ReaderChromeCommands::grow()'])
    methods+='\n'+'\n'.join(definition(cpp,s) for s in ['ReaderToolbarUi::ReaderToolbarUi(', 'const char* ReaderToolbarUi::text(', 'XpUiReaderRows ReaderToolbarUi::resources()', 'bool ReaderToolbarUi::render()'])
    # Native source text and actual scene construction stay intact; only the physical draw target is fake.
    ui=replace_once(ui,'// ACTUAL_TOOLBAR\n',declarations+methods+'\n')
    leaf=definition(native,'std::string_view view(XpAppText value)')+'\nstatic auto paint = '+native_leaf(native,'toolbar_paint')+';\n'
    ui=replace_once(ui,'// ACTUAL_TOOLBAR_LEAF',leaf)
    adapters=(HERE/'book_text_adapters.cpp').read_text()
    bodies='\n'.join(definition(native,s) for s in ['std::string_view view(XpAppText value)','const std::string& borrowed(XpAppNativeText value)','void emitSession(', 'void emitExit('])+'\n'
    for name in ['path_remove_tree','path_remove','path_mkdir','path_rename','file_open_read','file_open_write','font_sizes','font_text_advance','inflate_oom_log','emit_open','popup_show','clippings_insert']:
        bodies+='static auto '+name+' = '+native_leaf(native,name)+';\n'
    app=(root/'src/activities/RustAppResources.cpp').read_text()
    bodies+=definition(app,'std::string owned(XpAppText value)').replace('std::string owned(', 'std::string app_owned(')+'\n'
    for name in ['recent_missing','path_exists','bitmap_open','header_draw','footer_draw','popup_draw','text_draw','text_width','text_wrap','text_truncate','text_box_height']:
        bodies+='static auto app_'+name+' = '+native_leaf(app,name)+';\n'
    adapters=replace_once(adapters,'// ACTUAL_TEXT_ADAPTERS',bodies)
    return source+ui+adapters+(HERE/'book_text_oracle.cpp').read_text()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--archive');parser.add_argument('--baseline-cstr',action='store_true');parser.add_argument('--out',type=Path);args=parser.parse_args()
    source=composition(ROOT)
    if args.baseline_cstr:
        old=subprocess.check_output(['git','show','f73eb382:src/activities/reader/RustEpubResources.cpp'],text=True)
        source=replace_once(source,definition(source,'const char* pathText(XpAppText value)'),definition(old,'const char* pathText(XpAppText value)'))
    with tempfile.TemporaryDirectory(prefix='reader-book-text-') as directory:
        work=Path(directory);cpp=work/'text.cpp';binary=work/'text';cpp.write_text(source)
        if args.out:args.out.mkdir(parents=True,exist_ok=True);(args.out/'composition.cpp').write_text(source)
        cmd=compile_command(ROOT,work,cpp,binary,args.archive)
        cmd[1:1]=['-I'+str(ROOT/'freeink-sdk/libs/ui/FreeInkUI/include'),'-I'+str(ROOT/'freeink-sdk/libs/assets/Icons/include'),'-DFREEINK_UI_COMPONENT_STYLE=0']
        cmd.insert(-2,str(ROOT/'freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp'))
        subprocess.run(cmd,check=True)
        if not args.baseline_cstr:
            for mode in ['bad-epub','bad-app']:
                result=subprocess.run([str(binary),mode],capture_output=True,text=True)
                assert result.returncode==-signal.SIGABRT and 'value.data[value.len] == 0' in result.stderr,(mode,result.returncode,result.stderr)
                if args.out:(args.out/(mode+'.stderr')).write_text(result.stderr)
        book,cache=make_fixture(work)
        # Exact source payloads contain no sentinel padding; cache strings carry lengths only.
        cases=[b'',b'x',b'a'*15,b'b'*16,b'c'*8801,b'left\0right',b'bad\xff\xfeutf8']
        import struct
        original=(cache/'book.bin').read_bytes();toc=struct.unpack_from('<I',original,9+len(b''.join(struct.pack('<I',len(x))+x for x in [b'Title',b'Author',b'en',b'',b'a.xhtml']))+8)[0]
        for index,value in enumerate(cases):
            (work/'expected').write_bytes(value)
            (cache/'book.bin').write_bytes(original[:toc]+struct.pack('<I',len(value))+value+original[toc+4+len(b'Chapter'):])
            result=subprocess.run([str(binary),str(book),str(cache),str(work/'expected')],capture_output=True,text=True)
            if args.out:
                (args.out/f'case-{index}.stdout').write_text(result.stdout);(args.out/f'case-{index}.stderr').write_text(result.stderr)
            assert result.returncode==0,(index,len(value),result.returncode,result.stderr[-3000:])
    print(json.dumps(dict(cases=len(cases),composition_sha256=hashlib.sha256(source.encode()).hexdigest(),actual=['ReaderBook/cache TOC', 'RustEpubResources toolbar_paint', 'ReaderToolbarUi text/resources/render', 'Rust toolbar scene', 'FreeInkUI text layout', 'audited reader/Home C-string adapters', 'checked TOC allocation failure and immediate re-query'],physical_fake='GfxRenderer drawing/metrics and private host storage')))

if __name__=='__main__':main()
