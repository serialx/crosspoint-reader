"""Compose Section physical bindings; no reader policy is reconstructed here."""
from pathlib import Path
import sys
repo, output = map(Path, sys.argv[1:3])
trace = sys.argv[3:] == ['--trace']
if sys.argv[3:] and not trace:
    raise ValueError('Unknown physical fixture option')
sys.path.insert(0, str(repo / 'test/reader_lifecycle'))
from test_popup_resource import definition, native_leaf
native = (repo / 'src/activities/reader/RustEpubResources.cpp').read_text()
helpers = '\n'.join(definition(native, signature) for signature in (
    'XpAppText text(const std::string& value)', 'XpAppText text(const char* value)',
    'const char* pathText(XpAppText value)', 'std::string owned(XpAppText value)',
    'std::string_view view(XpAppText value)',
    'static const GfxRenderer& sectionRenderer(', '[[maybe_unused]] int32_t textAdvance(',
    'void prepareText(', '[[maybe_unused]] uint8_t startsRtl(', '[[maybe_unused]] uint8_t visualOrder('))
metrics = definition(native, 'XpParaMetrics sectionMetrics{') + ';'
if trace:
    anchor = 'nullptr,  // Layout tracing is installed only by the host instrumenter.'
    if metrics.count(anchor) != 1:
        raise ValueError('Layout observer anchor must occur once')
    metrics = metrics.replace(anchor,
        '[](void*, XpParaSpan record) { LayoutTrace::stream.write(reinterpret_cast<const char*>(record.data), record.len); },')
fields = ('build_metrics', 'build_allocator', 'font_variant', 'font_line_height', 'font_ready',
          'font_codepoint', 'hyphen_language', 'file_position', 'file_flush', 'image_supported',
          'image_dimensions', 'framebuffer_lend', 'framebuffer_restore', 'fonts_release_caches', 'file_open_read',
          'file_open_write', 'file_read', 'file_write', 'file_seek', 'file_size', 'file_close',
          'path_remove', 'path_rename', 'path_mkdir', 'heap_snapshot', 'log', 'log_enabled',
          'native_path_hash')
assignments = '\n'.join('value.' + field + ' = ' +
    ('paragraphAllocator' if field == 'build_allocator' else native_leaf(native, field)) + ';' for field in fields)
# Fatal hardware restart is a physical fixture action, never a policy return replacement.
assignments = assignments.replace('ESP.restart();', 'std::abort();')
# Host ESP fixtures use size_t; the firmware snapshot stores 32-bit counters.
assignments = assignments.replace('ESP.getFreeHeap(), ESP.getMaxAllocHeap()',
    'static_cast<uint32_t>(ESP.getFreeHeap()), static_cast<uint32_t>(ESP.getMaxAllocHeap())')
source = r'''
#include "ReaderSectionFixture.h"
#include <GfxRenderer.h>
#include <FontCacheManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <BidiUtils.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <functional>
#include "physical_defaults.h"
const XpParaAllocator* paragraphAllocator();
namespace {
GfxRenderer* target = nullptr;
const std::function<bool()>* input = nullptr;
bool cacheHeld = false;
size_t liveBuffers = 0;
''' + helpers + '\n' + metrics + r'''
const XpAppResources resources = [] {
 auto value = unexpectedAppResources();
 value.path_exists = [](XpAppText path) -> uint8_t { return Storage.exists(pathText(path)); };
 value.buffer_allocate = [](XpAppMemoryDomain domain, size_t bytes) -> uint8_t* {
   assert(domain == XP_APP_MEMORY_DEFAULT);
   auto* data = static_cast<uint8_t*>(::operator new(bytes, std::nothrow));
   if (data) ++liveBuffers;
   return data;
 };
 value.buffer_release = [](XpAppMemoryDomain domain, uint8_t* data) { assert(domain == XP_APP_MEMORY_DEFAULT);
   if (data) { assert(liveBuffers); --liveBuffers; }
   ::operator delete(data); };
 return value;
}();
const XpEpubLeaves leaves = [] {
 auto value = unexpectedEpubLeaves();
''' + assignments + r'''
 value.cache_lock = [] { assert(!cacheHeld); cacheHeld = true; };
 value.cache_unlock = [] { assert(cacheHeld); cacheHeld = false; };
 value.protection_open = [](XpAppText, XpAppTextSink, void*) -> void* { return nullptr; };
 value.input_raw_contact = []() -> uint8_t { assert(input && *input); return (*input)(); };
 return value;
}();
const XpAppRuntime runtime = [] {
 auto value = unexpectedAppRuntime();
 value.now_ms = []() -> uint32_t { return millis(); };
 value.delay_ms = [](uint32_t ms) { delay(ms); };
 return value;
}();
}
const XpAppResources& sectionFixtureResources() { return resources; }
const XpEpubLeaves& sectionFixtureLeaves(GfxRenderer& renderer) {
 if (!target) { target = &renderer; sectionMetrics.context = target; }
 assert(target == &renderer);
 return leaves;
}
const XpAppRuntime& sectionFixtureRuntime() { return runtime; }
void sectionFixtureYield(const std::function<bool()>* value) { input = value; }
size_t sectionFixtureLiveBuffers() { return liveBuffers; }
bool sectionFixtureCacheHeld() { return cacheHeld; }
'''
if trace:
    source = '#include "LayoutTrace.h"\n' + source
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(source)
