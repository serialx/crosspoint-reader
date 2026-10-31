"""Compare deterministic mutations with hardened C; permit only earlier rejection."""
import gzip
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import zlib


def corpus():
    rng = random.Random(6001)
    cases = []
    # Frozen zlib 1.2.12 seeds: empty, literal, repeats, alphabet, long run,
    # and a stored block. Corpus bytes do not depend on the host compressor.
    seeds = {0: ['0300', '730400', '4b4c4a4e49a4000300', '6360646266616563e7e0e4e2e6e1e5e3171014121611151397909492969195930700', '4b4c1c05a360148c540000', '010300fcff616263'], 1: ['789c030000000001', '789c73040000420042', '789c4b4c4a4e49a4000300206d18a1', '789c6360646266616563e7e0e4e2e6e1e5e3171014121611151397909492969195930700157001f1', '789c4b4c1c05a360148c540000f3788410', '7801010300fcff616263024d0127']}
    for wrapped in (0, 1):
        for encoded in seeds[wrapped]:
            original = bytes.fromhex(encoded)
            variants = [original]
            variants += [original[:i] for i in range(len(original))]
            variants += [original[:i] + bytes([v ^ (1 << bit)]) + original[i + 1:]
                         for i, v in enumerate(original) for bit in range(8)]
            if encoded == seeds[wrapped][-1]:
                offset = 3 if wrapped else 1
                for length in (0, 1, 255, 256, 32767, 32768, 65535):
                    for complement in (length, length ^ 65535):
                        fields = struct.pack('<HH', length, complement)
                        variants.append(original[:offset] + fields + original[offset + 4:])
            for data in variants:
                for ci, co in ((1, 1), (1, 257), (7, 1), (7, 4096), (4096, 4096)):
                    cases.append((wrapped, ci, co, data))
        for i in range(20000):
            data = rng.randbytes(rng.randrange(65))
            cases.append((wrapped, (1, 7, 4096)[i % 3], (1, 257, 4096)[i % 3], data))
    for raw in (bytes.fromhex(x) for x in ('03 02 00', '73 04 42 00', '03 3e 00', '03 7e 00')):
        for padding in (0, 14, 64):
            for ci, co in ((1, 1), (7, 257), (4096, 4096)):
                cases.append((0, ci, co, raw + b'\0' * padding))
    return cases


def main():
    cases = corpus()
    reference = gzip.decompress((Path(__file__).parent / 'fixtures/mutations-c.bin.gz').read_bytes())
    with tempfile.TemporaryDirectory() as directory:
        source, result = Path(directory) / 'input', Path(directory) / 'output'
        source.write_bytes(b''.join(struct.pack('<4I', w, ci, co, len(data)) + data
                                    for w, ci, co, data in cases))
        subprocess.run([sys.argv[1], str(source), str(result)], check=True, timeout=60)
        actual = result.read_bytes()
    expected = list(struct.iter_unpack('<i4I', reference))
    actual = list(struct.iter_unpack('<i4I', actual))
    assert len(actual) == len(expected) == len(cases)
    earlier = 0
    for i, (before, after) in enumerate(zip(expected, actual)):
        if before == after:
            continue
        earlier += 1
        # miniz_oxide checks invalid Huffman tables/codes before C does.
        # Consuming/emitting extra data, accepting invalid input, or changing
        # output at equal length is never an allowed difference.
        assert after[0] == -3, (i, before, after)
        assert after[1] <= before[1] and after[2] <= before[2], (i, before, after)
        if before[2] == after[2]:
            assert before[3] == after[3], (i, before, after)
        if before[0] != -3:
            wrapped, _, _, data = cases[i]
            try:
                zlib.decompress(data, 15 if wrapped else -15)
            except zlib.error:
                pass
            else:
                raise AssertionError(('rejected valid input', i, before, after))
    print(f'{len(cases)} mutation schedules passed; {earlier} earlier invalid-input rejections')


if __name__ == '__main__':
    main()
