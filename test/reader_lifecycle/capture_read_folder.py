"""Capture the pinned native read-folder helpers into a new, explicitly named directory."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
PARENT = 'b88726243ea49662e161eaab935ee11f303f9197'
SOURCE_PATH = 'src/activities/reader/EpubReaderActivity.cpp'
SIGNATURES = {
    'isInReadFolder': 'bool isInReadFolder(const std::string& path)',
    'buildReadFolderDestination': 'std::string buildReadFolderDestination(const std::string& srcPath)',
    'moveFinishedBookToReadFolder': 'void moveFinishedBookToReadFolder(const std::string& srcPath,',
}
HASH_FROM = 'std::hash<std::string>{}(dstPath)'
HASH_TO = 'fixturePathHash(dstPath)'
MARKER = '// PINNED_READ_FOLDER_DEFINITIONS'
MAX_ARTIFACT_BYTES = 200_000


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def definition(source, signature):
    """Copy a complete definition verbatim; these pinned bodies have balanced brace literals."""
    if source.count(signature) != 1:
        raise RuntimeError(f'expected exactly one pinned definition: {signature}')
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth and end < len(source):
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    if depth:
        raise RuntimeError(f'unclosed pinned definition: {signature}')
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', required=True, type=Path,
                        help='New directory for trace.txt, reference.cpp and manifest.json; must not exist.')
    parser.add_argument('--cxx', default='c++', help='Host C++ compiler executable.')
    args = parser.parse_args()
    output = args.capture.expanduser().absolute()
    if output.exists() or output.is_symlink():
        parser.error(f'refusing to overwrite existing capture destination: {output}')
    compiler = shutil.which(args.cxx)
    if compiler is None:
        parser.error(f'compiler not found: {args.cxx}')

    source_command = ['git', 'show', f'{PARENT}:{SOURCE_PATH}']
    source_bytes = subprocess.check_output(source_command, cwd=ROOT)
    source = source_bytes.decode('utf-8')
    constants = re.findall(r'^constexpr char READ_FOLDER\[\] = "/read";$', source, re.MULTILINE)
    if len(constants) != 1:
        raise RuntimeError('pinned READ_FOLDER declaration is missing or ambiguous')
    definitions = {name: definition(source, signature) for name, signature in SIGNATURES.items()}
    original = constants[0] + '\n\n' + '\n\n'.join(definitions.values())
    if original.count(HASH_FROM) != 1:
        raise RuntimeError('expected exactly one native path-hash expression')
    substituted = original.replace(HASH_FROM, HASH_TO, 1)
    template_bytes = (HERE / 'read_folder_reference.cpp').read_bytes()
    template = template_bytes.decode('utf-8')
    if template.count(MARKER) != 1:
        raise RuntimeError('reference template must contain exactly one definition marker')
    generated = template.replace(MARKER, substituted, 1).encode('utf-8')
    parent_parents = subprocess.check_output(
        ['git', 'show', '-s', '--format=%P', PARENT], cwd=ROOT, text=True).strip().split()
    compiler_version = subprocess.check_output([compiler, '--version'], text=True)
    compile_flags = ['-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-fsanitize=address,undefined']

    with tempfile.TemporaryDirectory(prefix='read-folder-reference-') as directory:
        work = Path(directory)
        translation_unit = work / 'reference.cpp'
        executable = work / 'reference'
        translation_unit.write_bytes(generated)
        compile_command = [compiler, *compile_flags, str(translation_unit), '-o', str(executable)]
        subprocess.run(compile_command, cwd=work, check=True)
        run_command = [str(executable)]
        trace = subprocess.check_output(run_command, cwd=work)

    lines = trace.splitlines()
    if sum(line.startswith(b'case ') for line in lines) != 100 or lines[-1:] != [b'cases 100']:
        raise RuntimeError('capture did not complete all 100 reference cases')
    invocation = [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]]
    manifest = {
        'schema': 1,
        'parent': PARENT,
        'parent_commit_parents': parent_parents,
        'source_path': SOURCE_PATH,
        'source_sha256': sha256(source_bytes),
        'source_command': source_command,
        'source_command_cwd': str(ROOT),
        'definitions': {name: {'signature': SIGNATURES[name], 'sha256': sha256(body.encode('utf-8'))}
                        for name, body in definitions.items()},
        'read_folder_declaration': constants[0],
        'read_folder_declaration_sha256': sha256(constants[0].encode('utf-8')),
        'original_extracted_sha256': sha256(original.encode('utf-8')),
        'substituted_extracted_sha256': sha256(substituted.encode('utf-8')),
        'template_sha256': sha256(template_bytes),
        'driver_sha256': sha256(Path(__file__).read_bytes()),
        'substitution': {
            'from': HASH_FROM,
            'to': HASH_TO,
            'count': 1,
            'convention': ('32-bit FNV-1a over unsigned path bytes, initial 2166136261, '
                           'xor byte then multiply by 16777619 modulo 2^32; unsigned decimal cache suffix.'),
            'event': 'path-hash records the raw path and unsigned result at the original expression site.',
        },
        'case_count': 100,
        'cases': {
            'classification': 17,
            'destination': 23,
            'move': 60,
            'destination_collision_counts': [0, 1, 2, 97, 98, 99],
            'collision_convention': ('N occupies the unsuffixed name and names (2) through (N); '
                                    'source files are not materialized in destination-only cases.'),
            'move_fields': ['sidecar_mask', 'cache', 'open_matches', 'failure', 'rollback_failure_mask'],
            'sidecar_mask': {'1': 'key', '2': 'rights'},
            'cache': {'0': 'empty oldCachePath', '1': 'absent directory',
                      '2': 'present directory with progress.bin', '3': 'present directory, rename fails'},
            'failure': {'0': 'none', '1': 'clipping move', '2': 'key rename', '3': 'rights rename'},
            'rollback_failure_mask': {'1': 'book rename', '2': 'key rename'},
            'move_matrix': {'success': 32, 'clipping_failure': 8, 'key_failure': 8, 'rights_failure': 12},
        },
        'fake_contract': {
            'storage': ('Sorted in-memory path map; trailing slash aliases a directory path; '
                        'mkdir is idempotent for directories; rename rejects missing source, occupied target '
                        'or configured failure and otherwise moves the full subtree.'),
            'clippings': ('One physical moveBook leaf atomically moves book and clipping association on success; '
                          'configured failure changes neither; no nested Storage API calls are fabricated.'),
            'recents': 'One source entry; updatePath records all four arguments and replaces path/cache.',
            'app_state': 'Open path and saved open path are separate; saveToFile records a single persistence call.',
            'trace': ('Production calls and logs in execution order, then sorted filesystem and clipping records, '
                      'recent entry/update count, live/saved open path/save count; fixture setup is silent.'),
        },
        'capture_command': invocation,
        'capture_command_shell': shlex.join(invocation),
        'compile_command': compile_command,
        'compile_command_cwd': str(work),
        'run_command': run_command,
        'run_command_cwd': str(work),
        'replay_compile_command': [compiler, *compile_flags, 'reference.cpp', '-o', 'reference'],
        'compiler_version': compiler_version,
        'artifacts': {
            'trace.txt': {'sha256': sha256(trace), 'bytes': len(trace)},
            'reference.cpp': {'sha256': sha256(generated), 'bytes': len(generated)},
        },
        'maximum_combined_artifact_bytes': MAX_ARTIFACT_BYTES,
    }
    manifest_bytes = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode('utf-8')
    artifacts = {'trace.txt': trace, 'reference.cpp': generated, 'manifest.json': manifest_bytes}
    total = sum(map(len, artifacts.values()))
    if total >= MAX_ARTIFACT_BYTES:
        raise RuntimeError(f'capture artifacts exceed size limit: {total} >= {MAX_ARTIFACT_BYTES}')
    # A race cannot turn this explicit new capture into an overwrite.
    output.mkdir(parents=True, exist_ok=False)
    for name, contents in artifacts.items():
        with (output / name).open('xb') as file:
            file.write(contents)
    print(f'Captured 100 native cases: {total} bytes in {output}')


if __name__ == '__main__':
    main()
