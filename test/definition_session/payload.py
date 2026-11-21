"""Compare current native payload methods to frozen parent methods with shared physical stubs."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'test'),str(ROOT/'test/reader_lifecycle')]
from rust_host import rust_link_flags
from test_app_results import block
parser=argparse.ArgumentParser();parser.add_argument('--evidence',type=Path,required=True);parser.add_argument('--archive');args=parser.parse_args();args.evidence.mkdir(parents=True,exist_ok=True)
reference=ROOT/'test/test_recursive_dictionary.py';context={'__file__':str(reference),'__name__':'definition_payload_reference'}
exec(compile(reference.read_text().split('with tempfile.TemporaryDirectory() as directory:',1)[0],str(reference),'exec'),context)
s=context['harness'].split('int main()',1)[0]
# Physical geometry is supplied identically to the frozen native bodyArea and Rust query.
s='#include <variant>\n#include "util/DictionaryBuffer.h"\n'+s
s=s.replace('const std::string& text, char) { return text; }','std::string_view text, char) { return std::string(text); }')
s=s.replace('Renderer& renderer, const std::string&, uint16_t','Renderer& renderer, std::string_view, uint16_t')
s=s.replace('  std::string lastQuery;', '''  bool lookup(const char* word, DictionaryBuffer& out, std::string& title, LookupResult* code) {
    std::string text;
    if (!lookup(word, text, title, code)) return false;
    memcpy(out.resize(text.size()), text.data(), text.size());
    return true;
  }
  std::string lastQuery;''')
s=s.replace('using GfxRenderer = Renderer;', 'using GfxRenderer = Renderer;\nstatic XpUiDefinitionLayout testLayout{};\nstatic int lastQueryX,lastQueryY;\nXpUiDefinitionLayout dictionaryUiDefinitionLayout(const GfxRenderer&){return testLayout;}')
s=s.replace('BodyArea bodyArea() const { return {0, 0, 12, 20}; }','BodyArea bodyArea() const;')
s+='\n'+block(context['viewer'],'DictionaryDefinitionActivity::BodyArea DictionaryDefinitionActivity::bodyArea')
s=s.replace('template<typename... Args> explicit ClipSelectionActivity(Args&&...) {}','template<class Pages> ClipSelectionActivity(Renderer&,MappedInputManager&,Pages,int x,int y,Selection,int,int){selectionOriginX=x;selectionOriginY=y;}')
s=s.replace('struct ClipSelectionActivity {','static int selectionOriginX,selectionOriginY;\nstruct ClipSelectionActivity {')
s=s.replace('struct Dictionary {','struct Dictionary {\n  bool definitionsAreHtml() const {return false;}')
h=(ROOT/'src/activities/reader/DefinitionPayload.h').read_text();cpp=(ROOT/'src/activities/reader/DefinitionPayload.cpp').read_text()
s+='\n'+block(h,'struct XpDefinitionNativeTransfer {')+';\n'+block(h,'class DefinitionPayload {')+';\n'
for signature in ['DefinitionPayload& DefinitionPayload::get(XpDefinitionPayloadStorage*','const DefinitionPayload& DefinitionPayload::get','\nDefinitionPayload::Slot& DefinitionPayload::slot','const DefinitionPayload::Slot& DefinitionPayload::slot','void DefinitionPayload::Slot::releasePayload','struct DefinitionPayload::PlainSelectionLine {','bool DefinitionPayload::prepareDefinition','bool DefinitionPayload::convertHtmlToPlain','bool DefinitionPayload::layoutHtmlPages','int DefinitionPayload::measureSpan','bool DefinitionPayload::reserveLines','bool DefinitionPayload::wrapText','bool DefinitionPayload::buildPlainSelectionPages','void* DefinitionPayload::selectionChild','uint8_t DefinitionPayload::constructPlain','uint8_t DefinitionPayload::constructNative']:
 s+='\n'+block(cpp,signature)+(';' if signature.startswith('struct ') else '')+'\n'
lookup=(ROOT/'src/activities/reader/RustReaderLookup.cpp').read_text();s+='\nnamespace readerLookup { struct PayloadLookup{Dictionary dictionary;std::string query,headword;DictionaryBuffer definition;}; PayloadLookup& get(XpEpubLookupStorage* p){return *reinterpret_cast<PayloadLookup*>(p);}\n'+block(lookup,'uint8_t payloadLookup')+'\n'+block(lookup,'uint8_t payloadFind')+'\n}\nstatic Dictionary dictionary;\n'
leaf=block((ROOT/'src/activities/reader/RustEpubResources.cpp').read_text(),'[](XpUiDefinitionLayout* out, int32_t x, int32_t y)').replace('const bool headerBackHit','lastQueryX=x;lastQueryY=y;\n        const bool headerBackHit')
s+=Path(__file__).with_name('payload.cpp.in').read_text().replace('// LAYOUT_LEAF',leaf)
source=args.evidence/'payload.cpp';source.write_text(s);binary=args.evidence/'payload'
owner=args.evidence/'owner.cpp';owner.write_text(context['owner_source']);owner_o=args.evidence/'owner.o'
commands=[['c++','-std=c++20','-DXP_APP_TEST', '-DXP_SD_FONT_CATALOG', '-I', str(ROOT / 'test/sdfont_legacy/abi'),'-UNDEBUG','-O0','-DXP_FFI_HOST_FAULT_INJECTION','-fsanitize=address,undefined','-I',str(ROOT/'test/reader_lifecycle'),'-I',str(ROOT/'test/ui_runtime'),'-I',str(ROOT/'src/activities'),'-I',str(ROOT/'rust/crates/xp-ffi/include'),'-c',str(owner),'-o',str(owner_o)],['c++','-std=c++20','-DXP_APP_TEST', '-DXP_SD_FONT_CATALOG', '-I', str(ROOT / 'test/sdfont_legacy/abi'),'-UNDEBUG','-O0','-DXP_FFI_HOST_FAULT_INJECTION','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(ROOT/'src'),'-I',str(ROOT/'lib/Memory'),'-I',str(ROOT/'test'),'-I',str(ROOT/'lib/MiniBidi'),'-I',str(ROOT/'lib/Utf8'),'-I',str(ROOT/'test/minibidi_arabic/stubs'),str(source),str(owner_o),str(ROOT/'lib/MiniBidi/RustBidi.cpp'),str(ROOT/'lib/Utf8/Utf8.cpp'),*rust_link_flags(args.archive),'-o',str(binary)],[str(binary)]]
records=[]
for i,cmd in enumerate(commands):
 r=subprocess.run(cmd,text=True,capture_output=True);(args.evidence/f'{i}.log').write_text(r.stdout+r.stderr);records.append({'command':cmd,'exit':r.returncode})
 if r.returncode:print(r.stderr[-5000:]);break
(args.evidence/'results.json').write_text(json.dumps({'steps':records,'generated_sha256':hashlib.sha256(s.encode()).hexdigest(),'scope':'actual current payload methods vs frozen native methods; synthetic font, HTML parser, geometry, dictionary and Page construction; real Rust body calculation, Bidi and UTF8'},indent=2)+'\n')
assert len(records)==3 and all(r['exit']==0 for r in records),records[-1]
print((args.evidence/'2.log').read_text())
