import pathlib, sys
repo, build = map(pathlib.Path, sys.argv[1:3])
# Reuse the exact parser mutation corpus, including public font fixtures.
source = (repo / 'test/sdfont_diff/run.py').read_text()
source = source.replace("HERE = pathlib.Path(__file__).resolve().parent", "HERE = pathlib.Path(" + repr(str(repo / 'test/sdfont_diff')) + ")")
source = source.replace("f'{kind}_{v}'", "f'h6_sd_{v}'")
source = source.replace("    compare('sdalloc', paths)", "")
source = source.replace("compare('sdname', [])", "")
source = source.replace("    print(f'{kind}: ", """    required = ['Invalid magic bytes', 'Unsupported version:', 'Invalid style count:',
                'Invalid styleId ', 'Duplicate styleId ', 'unreasonable counts',
                '(first 0x', 'invalid interval layout']
    for event in required:
        if event.encode() not in outputs[0]:
            raise SystemExit('Uncovered SD header event: ' + event)
    print(f'{kind}: """)
sys.argv = [sys.argv[0], '--build-dir', str(build)] + sys.argv[3:]
exec(compile(source, str(repo / 'test/sdfont_diff/run.py'), 'exec'))
