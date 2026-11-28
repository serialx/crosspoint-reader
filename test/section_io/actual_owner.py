"""Explicit projection of real Book/ZIP operations onto the frozen legacy Section trace."""
from pathlib import Path
import base64
import hashlib
import json
import re
import struct
import subprocess
import tempfile
import zipfile


def project(trace, payload_offset, payload_size):
    # The frozen Section I/O input fits in one 8192-byte synthetic-book stream read.
    if not 0 < payload_size < 8192:
        raise ValueError('Legacy Section stream mapping requires its original single-chunk fixture')
    output, operations = [], []
    construction, logical = False, 0
    for raw in trace.splitlines():
        line = raw
        if line.startswith('log DBG EBP Loading ePub:'):
            if construction:
                raise ValueError('Nested Book construction in physical trace')
            construction = True
        reason = None
        if construction:
            reason = 'Book construction'
            if line.startswith('log DBG EBP Loaded ePub:'):
                construction = False
        elif line.startswith(('io ', 'close ')) and 'cache/book.bin' in line:
            reason = 'Book metadata cursor'
        elif line.startswith('log ') and line.split()[2] in ('EBP', 'BMC', 'ZIP'):
            reason = 'Book/ZIP diagnostic'
        elif '.reader-fixture.epub' in line:
            if line.startswith('io '):
                fields = line.split()
                kind, position, length = fields[2], int(fields[4]), int(fields[5])
                if kind in ('rb', 'close'):
                    fields[3] = 'book/chapter.xhtml'
                    line = ' '.join(fields)
                elif kind == 'read' and position == payload_offset:
                    if length != payload_size:
                        raise ValueError('Unexpected physical payload request')
                    fields[3:6] = ['book/chapter.xhtml', '0', '8192']
                    line = ' '.join(fields)
                elif kind in ('read', 'seek', 'seekCur'):
                    reason = 'ZIP envelope'
                else:
                    raise ValueError(f'Unmapped ZIP operation: {line}')
            elif line == 'close .reader-fixture.epub':
                line = 'close book/chapter.xhtml'
            else:
                raise ValueError(f'Unmapped ZIP trace line: {line}')
        if reason:
            if raw.startswith('io '):
                operations.append({'physical': int(raw.split()[1]), 'extra': reason, 'line': raw})
            continue
        if line.startswith('io '):
            fields = line.split()
            logical += 1
            physical = int(fields[1])
            fields[1] = str(logical)
            line = ' '.join(fields)
            operations.append({'physical': physical, 'logical': logical,
                               'physical_line': raw, 'logical_line': line})
        elif line.startswith('steps '):
            line = f'steps {logical}'
        output.append(line)
    if construction:
        raise ValueError('Actual Book construction failed outside the legacy logical mapping')
    return '\n'.join(output) + '\n', operations


def run(binary, mode, source, logical_fault=0, baseline_map=None):
    physical_fault = 0
    if logical_fault:
        if baseline_map is None:
            raise ValueError('A frozen baseline map is required before injecting a logical fault')
        physical_fault = next(row['physical'] for row in baseline_map if row.get('logical') == logical_fault)
    with tempfile.TemporaryDirectory(prefix='section-actual-owner-') as directory:
        root = Path(directory)
        (root / 'book').mkdir()
        (root / 'book/chapter.xhtml').write_bytes(source)
        process = subprocess.run([str(binary), mode, str(physical_fault)], cwd=root,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        raw = process.stdout.decode(errors='replace')
        if process.returncode:
            return {'status': process.returncode, 'trace': raw, 'files': {}, 'physical_trace': raw,
                    'physical_fault': physical_fault, 'logical_fault': logical_fault, 'mapping': []}
        with zipfile.ZipFile(root / '.reader-fixture.epub') as archive:
            if archive.namelist() != ['chapter.xhtml']:
                raise ValueError('Legacy mapping requires its exact single-entry ZIP fixture')
            entry = archive.getinfo('chapter.xhtml')
            if archive.read(entry) != source or entry.compress_type != zipfile.ZIP_STORED:
                raise ValueError('ZIP payload differs from the legacy chapter input')
        with (root / '.reader-fixture.epub').open('rb') as stream:
            stream.seek(entry.header_offset + 26)
            name_size, extra_size = struct.unpack('<HH', stream.read(4))
        payload = entry.header_offset + 30 + name_size + extra_size
        projected, mapping = project(raw, payload, entry.file_size)
        before = (root / '.reader-fixture.book').read_bytes()
        after = (root / 'cache/book.bin').read_bytes()
        if before != after:
            raise ValueError('Preloaded Book metadata changed during legacy Section case')
        files = {str(p.relative_to(root)): base64.b64encode(p.read_bytes()).decode()
                 for p in sorted((root / 'cache').rglob('*')) if p.is_file() and p != root / 'cache/book.bin'}
        return {'status': process.returncode, 'trace': projected, 'files': files,
                'physical_trace': raw, 'physical_fault': physical_fault, 'logical_fault': logical_fault,
                'metadata_sha256': hashlib.sha256(before).hexdigest(), 'mapping': mapping,
                'source_sha256': hashlib.sha256(source).hexdigest(), 'payload_offset': payload,
                'payload_size': entry.file_size}
