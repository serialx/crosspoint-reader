#!/usr/bin/env python3
"""Compare complete public SDK parser results against pre-adapter Expat output."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
CHUNKS = (1, 7, 31, 1024, 2048, 8192)


def fixtures():
    atom = '<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opds="http://opds-spec.org/2010/catalog" xmlns:os="http://a9.com/-/spec/opensearch/1.1/">'
    entry = '<entry><id>urn:book:1</id><title>Café 한글 &amp; Books</title><author><name>A. Writer</name></author><link rel="http://opds-spec.org/acquisition/open-access" type="application/epub+zip" href="/books/a.epub"/></entry>'
    cases = {}
    def add(name, mode, text):
        cases[name] = (mode, text.encode() if isinstance(text, str) else text)
    add('atom', 'feed', atom + '<title>Library</title>' + entry + '</feed>')
    add('prefix', 'feed', '<a:feed xmlns:a="urn:atom"><a:title>Names</a:title><a:entry><a:title>Book</a:title><a:link href="sub" type="application/atom+xml"/></a:entry></a:feed>')
    add('links', 'feed', atom + ''.join(f'<link rel="{rel}" href="/{rel}" type="application/atom+xml"/>' for rel in ['next','previous','first','last','search','http://opds-spec.org/shelf','http://opds-spec.org/wishlist','http://opds-spec.org/history']) + '<os:totalResults>150</os:totalResults><os:itemsPerPage>25</os:itemsPerPage><os:startIndex>26</os:startIndex>' + entry + '</feed>')
    add('prices_facets', 'feed', atom + '<link rel="http://opds-spec.org/facet" opds:facetGroup="Language" href="/en" title="English"/><entry><title>Paid</title><link rel="http://opds-spec.org/acquisition/buy" type="application/epub+zip" href="/buy"><opds:price currencycode="USD">4.99</opds:price></link><summary type="html">&lt;b&gt;Bold&lt;/b&gt; &amp; plain</summary><link rel="http://opds-spec.org/image" href="/cover.jpg"/></entry></feed>')
    add('cdata_pi', 'feed', atom + '<!--comment--><?meta value?><title><![CDATA[one ] bracket > two]]></title>' + entry.replace('Café 한글 &amp; Books', '<![CDATA[Title <&>]]>\r\nrest') + '</feed>')
    add('declared', 'feed', '<!DOCTYPE feed [<!ENTITY word "replacement"><!ATTLIST link type CDATA "application/epub+zip">]>' + atom + entry.replace('Café 한글 &amp; Books', '&word; &#169; &#x1F600;') + '</feed>')
    add('external_entity', 'feed', '<!DOCTYPE feed SYSTEM "https://invalid.example/unused.dtd">' + atom + entry.replace('Café 한글 &amp; Books', '&unknown; &copy;') + '</feed>')
    add('undefined_entity', 'feed', atom + entry.replace('Café 한글 &amp; Books', '&undefined;') + '</feed>')
    add('invalid_attribute_entity', 'feed', atom + entry.replace('/books/a.epub', '/&missing;.epub') + '</feed>')
    add('truncated', 'feed', atom + entry + '<entry><title>Unfinished')
    add('mismatch', 'feed', atom + entry + '</wrong>')
    add('duplicate_attribute', 'feed', atom + '<entry id="a" id="b">bad</entry></feed>')
    add('trailing', 'feed', atom + entry + '</feed>junk')
    add('long_fields', 'feed', atom + entry.replace('Café 한글 &amp; Books', 'long '*700) + '</feed>')
    add('caps', 'feed', atom + entry*70 + '</feed>')
    add('bom', 'feed', '\ufeff \n' + atom + entry + '</feed>')
    add('empty', 'feed', '')
    add('json_sniff', 'feed', '{"metadata":{"title":"JSON","numberOfItems":1},"publications":[{"metadata":{"title":"Book"},"links":[{"href":"a.epub","rel":"http://opds-spec.org/acquisition","type":"application/epub+zip"}]}]}')
    search = '<OpenSearchDescription xmlns="http://a9.com/-/spec/opensearch/1.1/"><Url type="text/html" template="/web?q={searchTerms}"/><Url type="application/atom+xml" template="/search?q={searchTerms}&amp;page={startPage?}"/></OpenSearchDescription>'
    add('search', 'search', search)
    add('search_prefix', 'search', '<os:OpenSearchDescription xmlns:os="urn:os"><os:Url type="application/atom+xml" template="/q/{searchTerms}"/></os:OpenSearchDescription>')
    add('search_truncated', 'search', search[:-7])
    add('search_invalid', 'search', search.replace('&amp;', '&missing;'))
    add('search_utf16', 'search', search.encode('utf-16'))
    dav = '<d:multistatus xmlns:d="DAV:"><d:response id="self"><d:href>/books/</d:href><d:collection/></d:response><d:response id="one"><d:href>/books/Caf%C3%A9.EPUB</d:href><d:displayname> Café &amp; 한글 </d:displayname><d:author>Author</d:author></d:response><d:response><d:href>sub/</d:href><d:collection/></d:response></d:multistatus>'
    add('dav', 'catalog', dav)
    add('dav_cdata', 'catalog', dav.replace(' Café &amp; 한글 ', '<![CDATA[Café <&>]]><!--x-->\r\nnext'))
    add('dav_truncated', 'catalog', dav[:-8])
    add('dav_external', 'catalog', '<!DOCTYPE d:multistatus SYSTEM "unused">'+dav.replace(' Café &amp; 한글 ', '&unknown;'))
    add('dav_attributes', 'catalog', dav.replace('id="one"', 'id="A&#x20;B"'))
    add('dav_long', 'catalog', dav.replace(' Café &amp; 한글 ', '가나다'*1000))
    add('dav_caps', 'catalog', '<multistatus>' + '<response><href>a.epub</href></response>'*205 + '</multistatus>')
    return cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--binary', required=True, type=Path)
    ap.add_argument('--capture', action='store_true')
    ap.add_argument('--api', action='store_true')
    args = ap.parse_args()
    results = {}
    with tempfile.TemporaryDirectory(prefix='expat-sdk-oracle-') as folder:
        for name, (mode, data) in fixtures().items():
            path = Path(folder) / (name + '.xml')
            path.write_bytes(data)
            if args.api:
                mode = 'api'
            for chunk in CHUNKS:
                out = subprocess.check_output([str(args.binary.resolve()), mode, str(path), str(chunk)])
                results[f'{name}/{chunk}'] = {'input_sha256': hashlib.sha256(data).hexdigest(), 'output': out.decode()}
    golden = ROOT / ('api-goldens.json.gz' if args.api else 'goldens.json.gz')
    if args.capture:
        if golden.exists():
            raise SystemExit('Refusing to replace existing goldens')
        golden.write_bytes(gzip.compress(json.dumps(results, sort_keys=True, ensure_ascii=False).encode(), mtime=0))
    else:
        expected = json.loads(gzip.decompress(golden.read_bytes()))
        if results != expected:
            differences = [key for key in results.keys() | expected.keys() if results.get(key) != expected.get(key)]
            print('Mismatches:', differences)
            for key in differences[:3]:
                print(key, 'EXPECTED', expected.get(key), 'ACTUAL', results.get(key))
            raise SystemExit(1)
    print(f'{len(results)} complete SDK consumer results match ({len(fixtures())} inputs, {len(CHUNKS)} schedules).')


if __name__ == '__main__':
    main()
