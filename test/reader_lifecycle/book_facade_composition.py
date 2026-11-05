"""Compose the actual native Epub facade and physical resource adapters."""
from pathlib import Path
import shutil
from test_popup_resource import definition, replace_once, native_leaf
from book_items_composition import compose


def facade_source(repo):
    common=(repo/'test/reader_lifecycle/book_items_oracle.cpp').read_text().split('extern "C" int64_t xp_epub_test_items')[0]
    for sig in ['class Txt {','class Epub {','struct BookMetadataCache {']:common=replace_once(common,definition(common,sig)+';','')
    common='#include <Epub.h>\n#include <Txt.h>\n#include <xp_ffi.h>\n'+common
    common=common.replace('static const XpAppResources resources','// APP_CSTR_HELPER\nstatic const XpAppResources resources')
    epub=(repo/'lib/Epub/Epub.cpp').read_text();native=(repo/'src/activities/reader/RustEpubResources.cpp').read_text()
    methods='\n'.join(definition(epub,sig) for sig in ['Epub::BorrowedBook::BorrowedBook(', 'Epub::Epub(void* book,','bool Epub::requireOwned() const','CssParser* Epub::getCssParser() const','const std::string& Epub::getCachePath() const','const std::string& Epub::getPath() const','const std::string& Epub::getTitle() const','const std::string& Epub::getAuthor() const','const std::string& Epub::getLanguage() const','bool Epub::extractItemToFile(','int Epub::getSpineItemsCount() const','int Epub::getTocItemsCount() const','size_t Epub::getCumulativeSpineItemSize(','BookMetadataCache::SpineEntry Epub::getSpineItem(','BookMetadataCache::TocEntry Epub::getTocItem(','int Epub::getTocIndexForSpineIndex(','int Epub::getSpineIndexForSize(','size_t Epub::getBookSize() const','float Epub::calculateProgress('])
    image=(repo/'lib/Epub/Epub/blocks/ImageBlock.cpp').read_text()
    facade=r'''
    struct ImageBlock {
     using ExtractFn=bool(*)(void*,const char*,const char*);
     static inline void* extractCtx=nullptr;
     static inline ExtractFn extractFn=nullptr;
     static void setExtractor(void*,ExtractFn);
     static void clearRenderFailures() {}
    };
    '''+definition(image,'void ImageBlock::setExtractor(')+'\n'+definition(native,'struct BookHandle {')+';\n'+definition(native,'BookHandle& book(void* handle)')+'\n'
    common=common.replace('static const XpEpubLeaves leaves',facade+'static const XpEpubLeaves leaves')
    common=compose(common,('file_open_write','file_write','path_remove','path_rename','path_remove_tree','path_mkdir','heap_snapshot','cache_lock','cache_unlock','book_facade_open','book_facade_close','book_set_extractor'),('path_exists',))
    # Item methods occur before the added read-only facade methods.
    pos=common.index('struct ImageBlock');common=common[:pos]+methods+'\n'+common[pos:]
    return common

def compile_command(repo, out, source, binary, archive=None):
    import sys
    sys.path.insert(0, str(repo / 'test'))
    from rust_host import rust_link_flags
    stubs = out / 'stubs'
    stubs.mkdir(exist_ok=True)
    shutil.copy2(repo / 'lib/Txt/Txt.h', stubs / 'Txt.h')
    storage = (repo / 'test/book_io/epub-stubs/HalStorage.h').read_text()
    storage = storage.replace('size_t read(void* buffer, size_t count)', 'int read(void* buffer, size_t count)')
    (stubs / 'HalStorage.h').write_text(storage)
    includes=['test/book_io/epub-stubs','lib/Epub','lib/Epub/Epub','lib/ZipFile','lib/Txt','lib/FsHelpers','lib/Utf8','lib/Memory','lib/Serialization','lib/expat','lib/hal','lib/miniz/src','test/reader_lifecycle','test/ui_runtime','test/chapter_html_slim_parser/stubs']
    sources=['lib/Epub/Epub/PackagePlatform.cpp','lib/Epub/Epub/css/CssPlatform.cpp','lib/expat/ExpatErrors.cpp','lib/ZipFile/ZipPlatform.cpp','lib/FsHelpers/FsHelpers.cpp','lib/Utf8/Utf8.cpp','lib/Txt/TxtToHtml.cpp','lib/miniz/src/InflateStream.cpp','lib/Memory/BuildScratch.cpp','lib/hal/HalMemory.cpp']
    cmd=['c++','-std=c++20','-O0','-g','-UNDEBUG','-include','algorithm','-DXP_FFI_HOST_FAULT_INJECTION','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(stubs),*[v for p in includes for v in ['-I',str(repo/p)]],str(source),*[str(repo/p) for p in sources],*rust_link_flags(archive=archive),'-o',str(binary)]
    return cmd


def facade_bridge_source(repo):
    return facade_source(repo) + r'''
#undef malloc
#undef free
extern "C" void* testNativeFacadeOpen(void* owner, const XpEpubBookView* view,
                                     const XpEpubBookBorrow* callbacks) {
  return leaves.book_facade_open(owner, view, callbacks);
}
extern "C" void testNativeFacadeClose(void* raw) { leaves.book_facade_close(raw); }
extern "C" void testNativeExtractor(void* raw) { leaves.book_set_extractor(raw); }
std::shared_ptr<Epub> testNativeBook(void* raw) { return book(raw).book; }
'''


def facade_inputs(repo):
    paths = ['lib/Epub/Epub.cpp', 'lib/Epub/Epub.h',
             'src/activities/reader/RustEpubResources.cpp', 'src/activities/RustAppResources.cpp',
             'test/reader_lifecycle/book_facade_composition.py',
             'test/reader_lifecycle/book_items_composition.py',
             'test/reader_lifecycle/book_store_composition.py',
             'test/reader_lifecycle/book_items_oracle.cpp',
             'test/reader_lifecycle/reader_book_app_fixture.inc',
             'test/reader_lifecycle/legacy_book_fixture.h']
    result = {repo / path for path in paths}
    for directory in ['lib/Epub/Epub', 'lib/Memory', 'lib/ZipFile', 'lib/Txt',
                      'test/book_io/epub-stubs']:
        result.update(p for p in (repo / directory).rglob('*') if p.is_file())
    return result
