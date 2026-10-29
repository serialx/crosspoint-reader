from pathlib import Path
import sys
root, out = map(Path, sys.argv[1:3])
s = (Path(sys.argv[3]) if len(sys.argv)>3 else root/'src/activities/reader/KOReaderSyncActivity.cpp').read_text()
parts=[]
for signature in ['void KOReaderSyncActivity::releaseEpub(', 'void KOReaderSyncActivity::snapshotResources(',
                  'void KOReaderSyncActivity::prepareText(', 'void KOReaderSyncActivity::render(']:
    if signature not in s and signature.endswith('::render('):
        # Before the cleanup override, the activity inherited this synchronous render.
        parts.append('void KOReaderSyncActivity::render(RenderLock&& lock) { KOReaderUiActivity::render(std::move(lock)); }')
        continue
    start=s.index(signature);end=s.index('{',start)+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    parts.append(s[start:end])
out.write_text('\n'.join(parts))
