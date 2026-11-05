from pathlib import Path
import sys
root, output = map(Path, sys.argv[1:3])
source = Path(sys.argv[3]) if len(sys.argv)>3 else root/'src/activities/reader/KOReaderUiActivity.cpp'
s = source.read_text()
parts=[]
for signature in ['XpUiInterest KOReaderUiActivity::rustInterest(', 'XpUiEffect KOReaderUiActivity::updateRust(',
                  'void KOReaderUiActivity::rustSceneFailed(', 'void KOReaderUiActivity::applyRustEffect(',
                  'void KOReaderUiActivity::publish(', 'void KOReaderUiActivity::markReturn(', 'void KOReaderUiActivity::cancelRejectedInput(']:
    start=s.index(signature);end=s.index('{',start)+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    parts.append(s[start:end])
output.write_text('\n'.join(parts))
