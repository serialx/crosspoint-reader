"""Adapt App composition fixtures to real ReaderBook storage and native facade."""
from pathlib import Path
from test_popup_resource import replace_once
from test_app_results import block
from book_store_composition import store_adapters


def historical_selection_source(root, source):
    # The coordinator retained this exact historical allocation counter contract.
    original = block(source, 'void enterCompositionReader()')
    adapted = replace_once(original,
                           'assert(xp_app_event(&event) == XP_APP_ACCEPTED); assertEntered();',
                           'assert(install() == 1); assertEntered();')
    source = replace_once(source, original, adapted)
    shared = (root / 'test/reader_lifecycle/book_selection_allocation.inc').read_text().split('void realSelectionAllocation(', 1)[0]
    observation = block((root / 'test/reader_lifecycle/child_session_fixture.h').read_text(), 'struct XpEpubTestChildObservation')
    shared = replace_once(shared, '// ACTUAL_CHILD_OBSERVATION', observation + ';')
    source = replace_once(source, 'void nativeFactoryFailure(', shared + '\nvoid nativeFactoryFailure(')
    assertion = '    assert(results.born == before && results.pages.empty() && !xp_epub_test_has_child(results.owner));'
    return replace_once(source, assertion, assertion + '\n    recordSelectionOutcome();')


def actual_child_source(root, source):
    here = root / 'test/reader_lifecycle'
    helper = (here / 'reader_book_app_fixture.inc').read_text()
    marker = '// The driver extracts the native borrowed-result producer into this App fixture.'
    source = replace_once(source, marker, helper + '\nnamespace { bool hasReaderSection(); void readerSectionBalanced(); }\nclass Epub;\nstd::shared_ptr<Epub> testNativeBook(void*);\n' + marker)
    source = source.replace('bindResultsPhysical(); bindProductionChildren();',
                            'bindResultsPhysical(); bindProductionChildren(); bindReaderBook(); bindReaderResults(); BookStoreAdapters::bind();')
    source = source.replace('assertEntered();', 'assertReaderEntered();')
    source = '#include <xp_ui_reader_chrome.h>\n' + source
    base = 'struct ReaderChromeListActivity : RustActivityAdapter {\n  using RustActivityAdapter::RustActivityAdapter;'
    source = replace_once(source, base, base + r'''
  ReaderChromeListActivity(const char*, GfxRenderer&, MappedInputManager&, XpUiReaderListKind kind, uint16_t rows)
      : RustActivityAdapter(kind == XP_UI_READER_CHAPTERS ? "chapter" : "bookmarks") {
    assert(rows == 24 && (kind == XP_UI_READER_CHAPTERS || kind == XP_UI_READER_BOOKMARKS));
  }
''')
    probe = r'''
void checkChildBookLoan(const std::string& kind, bool held) {
  if (kind != "chapter" && kind != "bookmarks") return;
  assert(readerFacades.size() == 1);
  const auto count = testNativeBook(readerFacades.begin()->first).use_count();
  assert(held ? count >= 3 : count == 2);
}
'''
    source = replace_once(source, 'std::shared_ptr<Epub> testNativeBook(void*);',
                          'std::shared_ptr<Epub> testNativeBook(void*);\n' + probe)
    for method in ('onEnter()', 'loop()', 'render(RenderLock&&)', 'onExit()'):
        signature = 'void Activity::' + method + ' {'
        source = replace_once(source, signature, signature + ' checkChildBookLoan(kind, true);')
    source = replace_once(source, 'Activity::~Activity() {',
                          'Activity::~Activity() { checkChildBookLoan(kind, false);')
    old = ('struct PhysicalBook { std::shared_ptr<int> book = std::make_shared<int>(0); };\n'
           'PhysicalBook& book(void* raw) { assert(raw == f().extractor); static PhysicalBook value; return value; }')
    new = ('struct PhysicalBook { std::shared_ptr<Epub> book; };\n'
           'PhysicalBook book(void* raw) { assert(readerFacades.at(raw).marker == f().extractor); return {testNativeBook(raw)}; }')
    source = replace_once(source, old, new)
    addition = r'''
void bindReaderResults() {
  bindReaderSections();
  leaves.bookmarks_load = [](void* raw, XpAppText path)->uint8_t {
    auto& h = f().handle(raw, Kind::Bookmarks); assert(text(path) == f().path && f().extractor);
    ++f().bookmarkLoads; f().physical("bookmarks-load", h.generation, h.id); return 1;
  };
  leaves.bookmarks_save = [](void*, XpAppText path)->uint8_t {
    borrowedUse("bookmark-save"); assert(text(path) == f().path); return 1;
  };
  static auto write = leaves.file_open_write;
  leaves.file_open_write = [](XpAppText path, XpAppText origin)->void* {
    assert(text(path) == readerCache + "/links.bin"); return write(literal("/cache/links.bin"), origin);
  };
  leaves.recents_add = [](const XpAppBook* row) {
    physical("recent-add", 0); assert(row && text(row->path) == f().path && text(row->title) == "Fixture" && f().extractor);
  };
}
'''
    marker = 'extern "C" void xp_epub_test_child_sync(void*);'
    observation = block((here / 'child_session_fixture.h').read_text(), 'struct XpEpubTestChildObservation')
    section = (here / 'reader_section_app_fixture.inc').read_text()
    section = observation + ';\nextern "C" void xp_epub_test_child_observe(void*, XpEpubTestChildObservation*);\n' + section
    source = replace_once(source, marker, section + addition + store_adapters(root) + marker)
    matrix = (here / 'book_selection_allocation.inc').read_text()
    matrix = replace_once(matrix, '// ACTUAL_CHILD_OBSERVATION', '')
    source = replace_once(source, 'void nativeFactoryFailure(', matrix + '\nvoid nativeFactoryFailure(')
    source = replace_once(source, 'else if (mode == "selection-metadata-failure") {',
                           'else if (mode.starts_with("selection-allocation-")) realSelectionAllocation(mode);\n  else if (mode == "selection-metadata-failure") {')
    source = replace_once(source, 'assert(results.section && results.rendered > 0 && !results.bound);',
                          'assert(hasReaderSection() && results.rendered > 0 && !results.bound);')
    source = source.replace('!results.section', '!hasReaderSection()')
    source = replace_once(source, 'auto* section = results.section;', 'const bool section = hasReaderSection();')
    source = replace_once(source, 'results.section == section && results.toolbar == toolbar',
                          'hasReaderSection() == section && results.toolbar == toolbar')
    source = replace_once(source, '  popFinalSleep();', '  popFinalSleep(); readerSectionBalanced();')
    source = replace_once(source, 'assert(results.sectionOpened == results.sectionClosed && state.handles.empty());',
                          'assert(readerSectionOpens == readerSectionCloses && state.handles.empty());')
    source = replace_once(source, 'results.returns, results.born, results.sectionOpened);',
                          'results.returns, results.born, readerSectionOpens);')
    source = replace_once(source, 'pages=0 sections=%u', 'pages=0 section_reads=%u')
    source = replace_once(source,
                          'legacyBook.section.section_page_count = [](void*) -> uint16_t { return compositionPages; };\n'
                          '    legacyBook.section.section_estimated_pages = [](void*) -> int32_t { return compositionPages; };',
                          'setReaderSectionPages(compositionPages);')
    return source
