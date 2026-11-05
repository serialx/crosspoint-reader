from pathlib import Path
import sys,subprocess,tempfile,difflib
import prepare_wifi_effects
ROOT=Path(__file__).resolve().parents[2]
def run(s,archive=None):
 with tempfile.TemporaryDirectory() as d:
  p=Path(d);(p/'main.cpp').write_text(s)
  args=['c++','-std=c++20','-pthread','-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(p/'main.cpp')]
  if archive:args += [str(ROOT/'test/cmake/xp_ffi_host.cpp'),archive]
  subprocess.run(args+['-o',str(p/'test')],check=True)
  return subprocess.check_output([str(p/'test')]).decode()
def normalize(text):
 return '\n'.join(line for line in text.splitlines() if line not in ['viewport-reset','reset-ui','clear-flash'])
for core in [False,True]:
 original=run(prepare_wifi_effects.source(core))
 candidate=run(prepare_wifi_effects.candidate(core),sys.argv[1])
 a,b=normalize(original),normalize(candidate)
 if a!=b:
  sys.stderr.writelines(difflib.unified_diff(a.splitlines(True),b.splitlines(True)))
  raise SystemExit(1)
print('WiFi entry/keyboard/scan/touch/connection/timeout/prompt effects match the frozen original')
