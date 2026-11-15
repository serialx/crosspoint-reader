"""Actual native item methods and physical leaves shared by composed owner tests."""
from pathlib import Path
from test_popup_resource import definition, native_leaf, replace_once
ROOT = Path(__file__).resolve().parents[2]

def compose(source, extra_leaves=(), extra_resources=()):
    native = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
    app = (ROOT / 'src/activities/RustAppResources.cpp').read_text()
    epub = (ROOT / 'lib/Epub/Epub.cpp').read_text()
    txt = (ROOT / 'lib/Txt/Txt.cpp').read_text()
    source = replace_once(source, '// ACTUAL_TXT_METHODS', '\n'.join(definition(txt, signature) for signature in (
        'bool Txt::isTxtOrMd(std::string_view path)', 'bool Txt::streamTxtToHtml(const std::string& filepath, Print& out)')))
    item_context = definition(epub, 'struct Epub::ItemContext') + ';'
    methods = '\n'.join(definition(epub, signature) for signature in (
        'XpBookIoServices Epub::itemServices(ItemContext& context)',
        'uint8_t* Epub::readItemContentsToBytes(', 'bool Epub::readItemContentsToStream(', 'bool Epub::getItemSize('))
    source = replace_once(source, '// ACTUAL_ITEM_METHODS', item_context + '\n' + methods)
    source = replace_once(source, '// ACTUAL_TEXT_HELPERS', '\n'.join(definition(native, signature) for signature in (
        'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)', 'std::string owned(XpAppText value)')))
    source = replace_once(source, '// ACTUAL_ALLOCATOR_LEAVES', '\n'.join('value.' + field + ' = ' + native_leaf(app, field) + ';' for field in ('buffer_allocate', 'buffer_release', *extra_resources)))
    fields = ('file_open_read', 'file_read', 'file_close', 'file_seek', 'file_size', 'scratch_claim', 'scratch_release',
              'protection_open', 'protection_close', 'protection_encrypted', 'protection_size', 'protection_stream',
              'inflate_oom_log', 'log', 'log_enabled', *extra_leaves)
    source = replace_once(source, '// ACTUAL_LEAVES', '\n'.join('value.' + field + ' = ' + native_leaf(native, field) + ';' for field in fields))
    if extra_resources:
        source = source.replace('// APP_CSTR_HELPER', definition(app, 'const char* cstr(XpAppText value)'))
    return source
