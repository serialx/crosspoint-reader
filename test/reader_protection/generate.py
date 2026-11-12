#!/usr/bin/env python3
"""Synthetic EPUBs with a published test key, never real book or device keys."""
from pathlib import Path
import argparse, zipfile, zlib
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

TEST_KEY = bytes(range(16))
CHAPTER = b'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Public protection test</title></head><body><h1>Public protection test</h1>' + b'<p>The same words must appear before and after the reader port.</p>' * 100 + b'</body></html>'
CONTAINER = b'<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/package.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'
OPF = b'<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">public-r3b-test</dc:identifier><dc:title>Public protection test</dc:title><dc:creator>Test generator</dc:creator><dc:language>en</dc:language></metadata><manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="chapter"/></spine></package>'

def generate(out):
    out.mkdir(parents=True, exist_ok=True)
    compressed = zlib.compress(CHAPTER, wbits=-15)
    padded = compressed + bytes([16-len(compressed)%16]) * (16-len(compressed)%16)
    iv = bytes(range(16, 32))
    encrypt = Cipher(algorithms.AES(TEST_KEY), modes.CBC(iv)).encryptor()
    encrypted = iv + encrypt.update(padded) + encrypt.finalize()
    manifest = f'''<?xml version="1.0"?><encryption xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><EncryptedData xmlns="http://www.w3.org/2001/04/xmlenc#"><EncryptionMethod Algorithm="http://www.w3.org/2001/04/xmlenc#aes128-cbc"/><CipherData><CipherReference URI="OPS/chapter.xhtml"/></CipherData><EncryptionProperties><EncryptionProperty><Compression xmlns="http://www.idpf.org/2016/encryption#compression" Method="8" OriginalLength="{len(CHAPTER)}"/></EncryptionProperty></EncryptionProperties></EncryptedData></encryption>'''.encode()
    for kind in ('protected', 'plain', 'unsupported', 'bad-xml', 'corrupt-content'):
        with zipfile.ZipFile(out / (kind+'.epub'), 'w') as book:
            book.writestr('mimetype', 'application/epub+zip')
            book.writestr('META-INF/container.xml', CONTAINER)
            book.writestr('OPS/package.opf', OPF)
            book.writestr('OPS/chapter.xhtml', CHAPTER if kind in ('plain','unsupported') else encrypted[:-8] if kind=='corrupt-content' else encrypted)
            if kind != 'plain':
                data = manifest.replace(b'aes128-cbc', b'unsupported') if kind=='unsupported' else b'<encryption><broken' if kind=='bad-xml' else manifest
                book.writestr('META-INF/encryption.xml', data, compress_type=zipfile.ZIP_DEFLATED)
    (out/'truncated.epub').write_bytes((out/'protected.epub').read_bytes()[:-22])
    (out/'sample.txt').write_bytes(b'A plain text book still takes the protection probe.\n')
    (out/'chapter.xhtml').write_bytes(CHAPTER)

if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);generate(p.parse_args().directory)
