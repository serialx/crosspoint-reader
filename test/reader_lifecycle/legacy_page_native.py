"""Select frozen Page/frame fragments for named synthetic-owner oracles.

All other extracted physical services remain the current production definitions.
Real arena/metadata/transfer compositions must never call this adapter.
"""
from pathlib import Path
import re
from test_popup_resource import definition, native_leaf

HERE = Path(__file__).resolve().parent
FIELDS = ('page_close', 'page_info', 'page_element', 'page_word', 'metadata_assign', 'metadata_clear', 'metadata_footnotes', 'metadata_links', 'metadata_footnote', 'metadata_link', 'page_word_font', 'page_line_font', 'page_word_y_offset', 'page_line_ruby_shift', 'page_render', 'page_load', 'render_bind_page', 'child_footnote', 'child_dictionary', 'child_selection', 'resources_open', 'resources_close', 'render_services', 'render_finish_page')

def source(native):
    legacy = (HERE / 'legacy_page_native.inc').read_text()
    for signature in ('struct NativeRenderFrame {', 'struct NativeResources {', 'NativeResources& resources(void* handle)'):
        native = native.replace(definition(native, signature), definition(legacy, signature))
    for field in FIELDS:
        old = native_leaf(legacy, field)
        if re.search(r'\.' + field + r'\s*=', native):
            native = native.replace(native_leaf(native, field), old)
        else:
            native += '\n.' + field + ' = ' + old + ',\n'
    return native
