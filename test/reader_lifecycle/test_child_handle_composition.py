"""Join shipping child factories and legacy callbacks through the real Rust App.

UI, Page and SDK objects are physical fakes; Book and Section ownership is real. Ownership, factory return values,
legacy facts/lifecycle/results and App navigation are production implementations.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from test_app_results import make_source, block, MODES, WIFI_MATRIX
from test_popup_resource import native_leaf, replace_once
from book_facade_composition import compile_command as facade_compile_command, facade_bridge_source, facade_inputs
from book_app_composition import actual_child_source, historical_selection_source
from child_handle_mutants import mutants, SANITIZER, PAGE_LIFETIME, INVALID_DOWNCAST

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

NATIVE = ROOT / 'src/activities/reader/RustEpubResources.cpp'
OWNER = ROOT / 'src/activities/OwnedLegacyActivity.h'
MANAGER = ROOT / 'src/activities/ActivityManager.cpp'
CHILDREN = {
    'menu': ('EpubReaderMenuActivity', 'reader', 'menu'),
    'chapter': ('EpubReaderChapterSelectionActivity', 'reader', 'chapter'),
    'percent': ('EpubReaderPercentSelectionActivity', 'reader', 'percent'),
    'text_settings': ('TextSettingsActivity', 'settings', 'text'),
    'footnote': ('EpubReaderFootnoteSelectActivity', 'reader', 'footnote'),
    'dictionary': ('DictionaryWordSelectActivity', 'reader', 'dictionary'),
    'selection': ('ClipSelectionActivity', 'reader', 'selection'),
    'bookmarks': ('EpubReaderBookmarksActivity', 'reader', 'bookmarks'),
    'clippings': ('EpubReaderClippingListActivity', 'reader', 'clippings'),
    'qr': ('QrDisplayActivity', 'reader', 'qr'),
    'wifi': ('WifiSelectionActivity', 'network', 'wifi'),
    'sync': ('KOReaderSyncActivity', 'reader', 'sync'),
}


def fixture(native, owner, manager, historical=False):
    source = make_source(ROOT, HERE)
    prefix = '#include <Memory.h>\n#include "util/ScreenshotInfo.h"\n#define LOG_ERR(...) ((void)0)\n'
    prefix += '''
static size_t failOuterAfter = SIZE_MAX, outerAttempt = 0;
void* operator new(size_t bytes, const std::nothrow_t&) noexcept {
  if (outerAttempt++ == failOuterAfter) return nullptr;
  return ::operator new(bytes);
}
'''
    source = prefix + source
    source = replace_once(source, 'struct ResultPage { int page; };',
                          'struct ResultPage { int page; ~ResultPage(); };\nusing Page = ResultPage;')
    physical = (HERE / 'child_handle_composition.inc').read_text()
    xtc = (ROOT / 'src/activities/reader/XtcReaderActivity.h').read_text()
    xtc_base = re.search(r'class XtcReaderActivity final : public (\w+)', xtc).group(1)
    physical = replace_once(physical, '// ACTUAL_XTC_BASE',
                            f'struct XtcReaderActivity final : {xtc_base} {{ using {xtc_base}::{xtc_base}; }};')
    for marker, path, signature in [
        ('BASE_CAPABILITY', 'Activity.h', 'virtual ReaderActivity* readerControls()'),
        ('READER_CAPABILITY', 'reader/ReaderActivity.h', 'ReaderActivity* readerControls()'),
        ('INTERVAL_IDENTITY', 'util/IntervalSelectionActivity.h', 'bool isReaderActivity()'),
    ]:
        actual = block((ROOT / 'src/activities' / path).read_text(), signature)
        physical = replace_once(physical, '// ACTUAL_' + marker, actual)
    classes = []
    retained_constructors = []
    for leaf, (name, directory, kind) in CHILDREN.items():
        header = (ROOT / 'src/activities' / directory / (name + '.h')).read_text()
        base = re.search(r'class ' + name + r' final : public (\w+)', header).group(1)
        extra = 'enum class Tab { Family };' if leaf == 'text_settings' else ''
        if leaf in ('chapter', 'bookmarks') and not historical:
            constructor = block((ROOT / 'src/activities' / directory / (name + '.cpp')).read_text(), name + '::' + name + '(')
            declaration = header[header.index('explicit ' + name + '('):]
            declaration = declaration[:declaration.index(';') + 1]
            retained = ('std::shared_ptr<Epub> epub; int currentTocIndex = 0; static constexpr int TOC_WINDOW = 24;'
                        if leaf == 'chapter' else 'std::shared_ptr<Epub> epub; std::string epubPath;')
            extra = retained + '\n' + declaration
            retained_constructors.append(constructor)
        elif leaf == 'selection':
            extra = '''enum class Mode : uint8_t { Clip, Lookup, DefinitionLookup };
  std::vector<std::unique_ptr<Page>> ownedPages;
  ClipSelectionActivity(GfxRenderer&, MappedInputManager&, std::vector<std::unique_ptr<Page>> pages,
                        int, int, int, int, Mode) : RustActivityAdapter("selection"), ownedPages(std::move(pages)) {}
''' + block(header, 'bool isReaderActivity()')
        elif leaf in ('dictionary', 'footnote'):
            initial = ', std::string initial' if leaf == 'dictionary' else ''
            observation = ('if (initial.size() == 4096) { borrowedUse("dictionary-create"); ++results.lookups; }'
                           if leaf == 'dictionary' else '')
            extra = f'''std::unique_ptr<Page> ownedPage;
  {name}(GfxRenderer&, MappedInputManager&, std::unique_ptr<Page> page, int, int{initial})
      : {base}("{kind}"), ownedPage(std::move(page)) {{ {observation} }}'''
        else:
            extra += f'\n template<class... Args> explicit {name}(Args&&...) : {base}("{kind}") {{}}'
        if leaf == 'menu':
            extra += '\n' + block(header, 'bool acceptsRemoteInput()')
        classes.append(f'struct {name} final : {base} {{\n{extra}\n}};')
    # Types with constructors using Results are declared after its observers.
    physical = physical.replace('// CHILD_TYPES', '')
    source = replace_once(source, block(source, 'struct Activity') + ';', physical)
    source = replace_once(source, block(source, 'Activity* nativeActivity(const void* object)'), '')
    fallback = source[source.index('void* child(const char* kind,'):source.index('void borrowedUse(const char* event)')]
    source = replace_once(source, fallback,
                          'void* child(const char* kind, std::vector<ResultPage*> pages = {}) { assert(false && "fallback child factory"); return nullptr; }')
    source = replace_once(source, block(source, 'struct OwnedLegacyActivity') + ';',
                          owner[owner.index('struct OwnedLegacyActivity'):])
    source = replace_once(source, block(source, 'void releasePage(ResultPage* page)'), '''
ResultPage::~ResultPage() {
  assert(results.pages.erase(this) == 1 && results.bound != this);
  physical("page-close");
}
void releasePage(ResultPage* page) { assert(page); delete page; }
''')
    additions = r'''
Activity::Activity(std::string value) : kind(std::move(value)), name(kind) { ++results.born; physical("child-construct"); }
void Activity::onEnter() { physical("child-enter", 0); assert(!entered); entered = true; }
void Activity::loop() { physical("child-loop", 0); assert(entered); }
void Activity::render(RenderLock&&) { physical("child-render", 1); assert(entered); }
void Activity::onExit() { physical("child-exit", 1); assert(entered); }
void Activity::prepareForSleep() { physical("child-prepare-sleep", 0); }
void borrowedUse(const char*);
'''
    additions += '\n'.join(classes + retained_constructors) + r'''
static GfxRenderer renderer;
static MappedInputManager input;
static GfxRenderer* target = &renderer;
static MappedInputManager* mapped = &input;
struct ActivityManager {
  MappedInputManager mappedInput;
  bool send(const XpAppEvent& event) { return xp_app_event(&event) == XP_APP_ACCEPTED; }
  void pushActivity(std::unique_ptr<Activity>&&);
};
static ActivityManager activityManager;
static struct { int value = 0; int& registry() { return value; } } sdFontSystem;
struct PhysicalBook { std::shared_ptr<int> book = std::make_shared<int>(0); };
PhysicalBook& book(void* raw) { assert(raw == f().extractor); static PhysicalBook value; return value; }
XpEpubPosition position(const XpEpubPosition& value) { return value; }
struct SavedProgressPosition { std::string xpath; float percentage; };
'''
    additions += block(native, 'std::string owned(XpAppText value)') + '\n'
    additions += block(manager, 'void ActivityManager::pushActivity(') + '\n'
    additions += block((ROOT / 'src/activities/Activity.cpp').read_text(), 'void Activity::startActivityForResult(') + '\n'
    additions += block(manager, 'void screenshot(const ScreenshotInfo& info') + '\n'
    for name in ('facts', 'enter', 'loop', 'exit', 'destroy', 'render', 'prepare_sleep',
                 'deliver_result', 'with_result', 'reader_turn', 'home_gesture', 'forced_refresh', 'screenshot_info'):
        additions += f'static const auto production_{name} = +{native_leaf(manager, name)};\n'
    source = source.replace('void* child(const char* kind,', additions + '\nvoid* child(const char* kind,', 1)
    source = replace_once(source, '''    if (!results.children.count(const_cast<void*>(raw))) return baseFacts(raw, a, b);
    physical("child-facts"); XpAppLegacyFacts facts{}; facts.kind = UINT8_MAX; return facts;''', '''
    if (f().handles.count(const_cast<void*>(raw))) return baseFacts(raw, a, b);
    auto facts = production_facts(raw, a, b);
    results.children.insert(const_cast<void*>(raw));
    physical("child-facts"); return facts;''')
    source = replace_once(source, 'physical("child-enter", 0); auto* a = nativeActivity(raw); assert(!a->entered); a->entered = true;', 'production_enter(raw);')
    source = replace_once(source, 'physical("child-exit", 1); assert(nativeActivity(raw)->entered);', 'production_exit(raw);')
    source = replace_once(source, '    delete static_cast<OwnedLegacyActivity*>(raw);', '    production_destroy(raw);')
    source = replace_once(source, 'physical("child-prepare-sleep", 0); assert(results.children.count(raw));', 'assert(results.children.count(raw)); production_prepare_sleep(raw);')
    source = replace_once(source, '    physical("sleep-render", 1); assert(token && token->lock);', '''
    if (results.children.count(raw)) { production_render(raw, token); return; }
    physical("sleep-render", 1); assert(token && token->lock);''')
    init = 'void bindProductionChildren() {\n'
    for leaf in CHILDREN:
        init += f'  leaves.child_{leaf} = {native_leaf(native, "child_" + leaf)};\n'
    init += '''  leaves.has_sync_credentials = []() -> uint8_t { return 0; };
  static auto readerLog = leaves.log;
  leaves.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    if (text(origin) == "CLIP") {
      assert(severity == 1 && (text(message) == "Failed to allocate clipping selection activity" ||
                              text(message) == "OOM: dictionary lookup activity" ||
                              text(message) == "OOM: clipping list"));
      physical("selection-allocation-log"); return;
    }
    if (text(origin) == "ERA") {
      assert(severity == 1 && text(message) == "OOM: EpubReaderFootnoteSelectActivity");
      physical("footnote-allocation-log"); return;
    }
    readerLog(severity, origin, message);
  };
  legacy.loop = production_loop;
  legacy.deliver_result = production_deliver_result;
  legacy.reader_turn = production_reader_turn;
  legacy.home_gesture = production_home_gesture;
  legacy.forced_refresh = production_forced_refresh;
  legacy.screenshot_info = production_screenshot_info;
}
'''
    source = source.replace('int main(int argc, char** argv)', init + '\nint main(int argc, char** argv)', 1)
    source = replace_once(source, 'bind(); bindResultsLifecycle(); bindResultsPhysical();',
                          'bind(); bindResultsLifecycle(); bindResultsPhysical(); bindProductionChildren();')
    source = replace_once(source, 'state.startAttempt(); assert(install() == 1); assertEntered();', '''
  state.startAttempt();
  XpAppEvent navigate{}; navigate.kind = XP_APP_NAVIGATE; navigate.value.navigate.kind = XP_APP_READER;
  navigate.value.navigate.value.reader = {wire(f().path), 0};
  assert(xp_app_event(&navigate) == XP_APP_ACCEPTED); assertEntered();''')
    source = replace_once(source, '  assert(xp_app_test_current_legacy() && !xp_app_test_current_epub());\n}', '''
  assert(xp_app_test_current_legacy() && !xp_app_test_current_epub());
  auto* raw = xp_app_test_current_legacy();
  const auto facts = legacy.facts(raw, 1, 1);
  assert(facts.reader == (nativeActivity(raw)->kind == "selection"));
  legacy.loop(raw);
  void* held = runtime.render_lock(); RenderLock lock; XpAppRenderToken token{&lock};
  legacy.render(raw, &token); runtime.render_unlock(held);
  assert(!legacy.reader_turn(raw, 1, 0));
  legacy.screenshot_info(raw, [](void*, const XpAppScreenshot* info) { assert(info); }, nullptr);
}''')
    source = replace_once(source, 'void queuePop(ActivityResult value, bool cancelled) {', '''
void queuePop(ActivityResult value, bool cancelled) {
  auto* child = xp_app_test_current_legacy(); assert(child);
  legacy.facts(child, 1, 1); legacy.loop(child);
  void* held = runtime.render_lock(); RenderLock lock; XpAppRenderToken token{&lock};
  legacy.render(child, &token); runtime.render_unlock(held);
  assert(!legacy.reader_turn(child, 1, 0));
''')
    extra = (HERE / 'child_handle_faults.inc').read_text()
    source = source.replace('int main(int argc, char** argv)', extra + '\nint main(int argc, char** argv)', 1)
    source = replace_once(source, '  Fixture state; fixture = &state;\n  results.wifiMatrix',
                          '  if (mode.starts_with("factory-fail-") || mode.starts_with("page-fail-") || mode.starts_with("capability-") || mode.starts_with("selection-") || mode.starts_with("nested-") || mode == "sync-replace" || mode == "pending-supersede") return specialComposition(mode);\n  Fixture state; fixture = &state;\n  results.wifiMatrix')
    return historical_selection_source(ROOT, source) if historical else actual_child_source(ROOT, source)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_CHILD_HANDLE_EVIDENCE_DIR'))
    parser.add_argument('--case')
    parser.add_argument('--mutants', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='child-handle-composition-') as tmp:
        out = args.evidence_dir or Path(tmp)
        out.mkdir(exist_ok=True, parents=True)
        inputs = {NATIVE, OWNER, MANAGER, ROOT/'lib/Memory/Memory.h', ROOT/'src/activities/Activity.cpp',
                  ROOT/'src/activities/Activity.h', ROOT/'src/activities/reader/ReaderActivity.h',
                  ROOT/'src/activities/reader/XtcReaderActivity.h', ROOT/'src/activities/util/IntervalSelectionActivity.h',
                  *HERE.glob('*.py'), *HERE.glob('*.inc'), *HERE.glob('*.h'), *HERE.glob('*.cpp')}
        inputs.update(facade_inputs(ROOT))
        inputs.update(ROOT/'src/activities'/directory/(name+'.h') for name,directory,_ in CHILDREN.values())
        inputs.update(ROOT/'src/activities/reader'/(name+'.cpp') for name in ('EpubReaderChapterSelectionActivity','EpubReaderBookmarksActivity'))
        hashes = {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(inputs)}
        (out/'inputs.json').write_text(json.dumps(dict(files=hashes,archive_sha256=hashlib.sha256(args.archive.read_bytes()).hexdigest()),indent=2)+'\n')
        native, owner, manager = NATIVE.read_text(), OWNER.read_text(), MANAGER.read_text()
        candidate = fixture(native, owner, manager)
        source = out / 'candidate.cpp'; source.write_text(candidate)
        binary = Path(tmp) / 'candidate'
        facade = out / 'facade.cpp'; facade.write_text(facade_bridge_source(ROOT))
        command = facade_compile_command(ROOT, out, facade, binary, archive=str(args.archive))
        command[command.index('-fsanitize=address,undefined')] = '-fsanitize=address,undefined,vptr'
        command[command.index('-o'):command.index('-o')] = ['-I', str(ROOT / 'src/activities'),
                                                         '-I', str(ROOT / 'src'), str(source)]
        (out/'compile.json').write_text(json.dumps(command,indent=2)+'\n')
        subprocess.run(command,check=True)
        cases = [(mode, str(flag)) for mode in MODES for flag in (0,1)]
        cases += [(mode,'0') for mode in (*WIFI_MATRIX,'stack-failure','pending-shutdown')]
        cases += [(f'factory-fail-{leaf}-{stage}', '0') for leaf in CHILDREN for stage in (0,1)]
        cases += [(f'page-fail-{count}-{stage}', '0') for count in (1,2,3) for stage in (0,1)]
        cases += [(x,'0') for x in ('sync-replace','nested-result','nested-failure','capability-reader',
                   'capability-interval','capability-selection','capability-xtc','selection-metadata-failure',
                   'selection-stack-failure','pending-supersede','selection-pending-shutdown')]
        if args.case: cases=[x for x in cases if x[0]==args.case]
        assert cases, args.case
        historical_source = out / 'historical-selection.cpp'
        historical_source.write_text(fixture(native, owner, manager, historical=True))
        historical_binary = Path(tmp) / 'historical-selection'
        historical_command = [str(historical_source) if x == str(source) else
                              str(historical_binary) if x == str(binary) else x for x in command]
        subprocess.run(historical_command, check=True)
        records=[]
        for mode, flag in cases:
            selected_binary = historical_binary if mode == "selection-metadata-failure" else binary
            result=subprocess.run([str(selected_binary),mode,flag],capture_output=True)
            label=f'{mode}-{flag}'
            (out/(label+'.stdout')).write_bytes(result.stdout);(out/(label+'.stderr')).write_bytes(result.stderr)
            assert result.returncode==0,(label,result.returncode,result.stderr.decode(errors='replace')[-5000:])
            records.append(dict(case=label,returncode=0))
        if args.case is None or args.case == 'selection-metadata-failure':
            probe = subprocess.run([str(binary), 'selection-allocation-count', '0'], capture_output=True)
            (out/'allocation-count.stdout').write_bytes(probe.stdout)
            (out/'allocation-count.stderr').write_bytes(probe.stderr)
            assert probe.returncode == 0, probe.stderr.decode(errors='replace')[-5000:]
            parent_outcome = re.search(rb'SELECTION_OUTCOME [^\n]+', (out/'selection-metadata-failure-0.stdout').read_bytes())[0]
            count = int(re.search(rb'REAL_SELECTION_ALLOCATION_COUNT (\d+)', probe.stdout)[1])
            allocation_records = []
            for index in range(count):
                fault = subprocess.run([str(binary), f'selection-allocation-{index}', '0'], capture_output=True)
                (out/f'allocation-{index}.stdout').write_bytes(fault.stdout)
                (out/f'allocation-{index}.stderr').write_bytes(fault.stderr)
                assert fault.returncode == 0, (index, fault.stderr.decode(errors='replace')[-5000:])
                assert re.search(rb'SELECTION_OUTCOME [^\n]+', fault.stdout)[0] == parent_outcome
                matched = re.search(rb'REAL_SELECTION_ALLOCATION_FAILURE (\d+) (\d+)', fault.stdout)
                assert matched and int(matched[1]) == index
                allocation_records.append(dict(fail_after=index, attempts=int(matched[2]), returncode=0))
            (out/'allocation-matrix.json').write_text(json.dumps(allocation_records, indent=2)+'\n')
            print('real book selection allocation matrix:', count, 'failure points passed')
        (out/'result.json').write_text(json.dumps(records,indent=2)+'\n')
        print('production child composition:',len(records),'cases passed')
        rejected = []
        if args.mutants:
            for mutant in mutants(native, owner, manager):
                name = mutant['name']
                changed = fixture(mutant['native'], mutant['owner'], mutant['manager'])
                mutant_source = out / (name + '.cpp'); mutant_source.write_text(changed)
                mutant_binary = Path(tmp) / name
                compile_command = [str(mutant_source) if x == str(source) else
                                   str(mutant_binary) if x == str(binary) else x for x in command]
                (out/(name+'.compile.json')).write_text(json.dumps(compile_command,indent=2)+'\n')
                subprocess.run(compile_command,check=True)
                result = subprocess.run([str(mutant_binary),mutant['case'],mutant['flag']],capture_output=True)
                (out/(name+'.stdout')).write_bytes(result.stdout)
                (out/(name+'.stderr')).write_bytes(result.stderr)
                stderr = result.stderr.decode(errors='replace')
                sanitizer = 'AddressSanitizer' in stderr or 'UndefinedBehaviorSanitizer' in stderr
                expected = mutant['expected_rejection']
                if expected == INVALID_DOWNCAST:
                    detected = 'UndefinedBehaviorSanitizer' in stderr and 'ReaderActivity' in stderr and 'downcast' in stderr
                elif expected == PAGE_LIFETIME:
                    page_assertion = (
                        'results.pages.erase(this) == 1 && results.bound != this' in stderr
                        and ('Assertion failed:' in stderr or ('Assertion `' in stderr and "' failed." in stderr))
                        and ('~ResultPage' in stderr))
                    detected = sanitizer or page_assertion
                else:
                    assert expected == SANITIZER
                    detected = sanitizer
                assert result.returncode != 0 and detected, (name,result.returncode,stderr[-5000:])
                rejected.append(dict(name=name,case=mutant['case'],expected_rejection=expected,
                                     returncode=result.returncode,source_sha256=hashlib.sha256(changed.encode()).hexdigest(),
                                     stderr_sha256=hashlib.sha256(result.stderr).hexdigest(),
                                     rejection_reason=('page-lifetime-assertion' if expected == PAGE_LIFETIME
                                                       and page_assertion else 'sanitizer')))
                (out/'mutants.json').write_text(json.dumps(rejected,indent=2)+'\n')
                print('rejected',name,flush=True)
        assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==digest for p,digest in hashes.items())
        (out/'scope.json').write_text(json.dumps(dict(cases=len(records),mutants_rejected=len(rejected),
            source_unchanged=True,production=['child factory bodies','wrapper and allocation helper','legacy callback bodies',
                'Activity native push and result delivery','Rust App lifecycle and EPUB session',
                'ReaderBook, ReaderSection and native Epub facade','retained chapter/bookmark constructors and book fields'],
            physical_fakes=['child UI implementations','tracked physical Page and its word observations','storage and renderer','Sleep terminal owner'],
            allocation_matrix='every Rust allocation on real selection capture path; exact historical one-attempt case retained separately',
            limitations=['physical Page stands in for native deserialization, separately covered by native compositions',
                'direct child method probes supplement emulator gestures',
                'native internal vector/string allocations remain O4/O5 debt',
                'network physical fakes do not validate real Wi-Fi connectivity']),indent=2)+'\n')

if __name__=='__main__':main()
