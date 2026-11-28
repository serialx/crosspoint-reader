import pathlib, sys
repo, out, reference = map(pathlib.Path, sys.argv[1:])
(out / 'reference').mkdir(parents=True, exist_ok=True)
for name in ['XtcParser.cpp', 'XtcParser.h', 'XtcTypes.h']:
    source = reference / name
    if not source.exists():
        continue
    (out / 'reference' / name).write_text(source.read_text().replace('namespace xtc', 'namespace reference_xtc'))
source = (repo / 'test/xtc_diff/XtcDiff.cpp').read_text()
source = '#include <Logging.h>\n' + source.replace('static void compare() {', 'static void compare() {\n  H6LogCase logs(context);')
source = source.replace('  if (!mutate) return;', """  for (int fault = 0; fault < 20; ++fault) {
    h6FailRead = fault;
    compare();
    h6FailRead = -1;
    h6FailSeek = fault;
    compare();
    h6FailSeek = -1;
  }
  if (!mutate) return;""")
source = source.replace('  std::cout << cases', '  h6AssertXtcCoverage();\n  std::cout << cases')
(out / 'XtcDiagnostic.cpp').write_text(source)
