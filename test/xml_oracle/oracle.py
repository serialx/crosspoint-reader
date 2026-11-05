#!/usr/bin/env python3
"""Record or verify vendored Expat events; never regenerate goldens during verification."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent.parent


def cases():
    yield "basic", b'<r z="first" a="second"><p:x xmlns:p="urn:x">hello</p:x><empty/></r>'
    yield "predefined", b'<r a="&amp;&lt;&gt;&quot;&apos;">&amp;&lt;&gt;&quot;&apos;</r>'
    yield "numeric", b'<r a="&#13;&#10;&#9;&#x1f642;">&#65;&#x41;&#13;&#10;&#9;&#x1f642;</r>'
    yield "line_endings", b'<r a="a\r\nb\rc\nd\te">a\r\nb\rc\nd</r>'
    yield "boundaries", b'<?before data?><r>a<!-- hi -->b<![CDATA[c\r\nd]]>e<?inside x?>f</r>'
    yield "utf8", '<r a="한글">é 한글 🙂</r>'.encode()
    yield "bom", b'\xef\xbb\xbf<r/>'
    yield "declared_entity", b'<!DOCTYPE r [<!ENTITY word "replacement">]><r a="&word;">&word;</r>'
    yield "external_entity", b'<!DOCTYPE r [<!ENTITY word SYSTEM "not-read">]><r a="&word;">&word;</r>'
    yield "undefined", b'<r>&nbsp;</r>'
    yield "undefined_attribute", b'<r a="x&nbsp;y"/>'
    yield "external_doctype", b'<!DOCTYPE r SYSTEM "not-read"><r a="x&nbsp;y">x&nbsp;y&unknown;</r>'
    yield "internal_doctype", b'<!DOCTYPE r []><r>&nbsp;</r>'
    yield "parameter_entity", b'<!DOCTYPE r [<!ENTITY % p SYSTEM "not-read">%p;]><r a="x&nbsp;y">&nbsp;</r>'
    yield "standalone_yes", b'<?xml version="1.0" standalone="yes"?><!DOCTYPE r SYSTEM "x"><r>&nbsp;</r>'
    yield "standalone_no", b'<?xml version="1.0" standalone="no"?><!DOCTYPE r SYSTEM "x"><r>&nbsp;</r>'
    yield "default_attributes", b'<!DOCTYPE r [<!ATTLIST r a CDATA "default" b NMTOKENS " a  b ">]><r b=" a\t b  c "/>'
    yield "duplicate_attribute", b'<r a="1" a="2"/>'
    yield "mismatch", b'<r><a></b></r>'
    yield "trailing_junk", b'<r/>junk'
    yield "two_roots", b'<r/><s/>'
    yield "bad_comment", b'<r><!-- a--b --></r>'
    yield "bad_cdata", b'<r>]]></r>'
    yield "bad_pi", b'<r><?xml bad?></r>'
    yield "bad_utf8", b'<r>\xf0\x28\x8c\x28</r>'
    yield "bad_name", b'<1r/>'
    yield "empty", b''
    for scalar in ("0", "xD800", "x110000", "xFFFE", "xFFFF", "1", "xZZ", "999999999999999999999"):
        yield "invalid_numeric_" + scalar, ('<r>&#' + scalar + ';</r>').encode()
    for encoding in ("UTF-8", "US-ASCII", "ISO-8859-1", "UTF-16", "windows-1252", "unknown"):
        yield "encoding_" + encoding, ('<?xml version="1.0" encoding="' + encoding + '"?><r>é</r>').encode()
    yield "latin1", b'<?xml version="1.0" encoding="ISO-8859-1"?><r>\xe9</r>'
    yield "utf16le", b'\xff\xfe' + '<?xml version="1.0" encoding="UTF-16"?><r>é</r>'.encode('utf-16le')
    yield "utf16be", b'\xfe\xff' + '<?xml version="1.0" encoding="UTF-16"?><r>é</r>'.encode('utf-16be')
    yield "long_token", ('<' + 'n' * 1100 + ' a="' + 'v' * 2100 + '"/>').encode()
    extended = {
        "empty_cdata": '<r><![CDATA[]]></r>',
        "partial_cdata": '<r><![CDATA[abc]',
        "cdata_brackets": '<r><![CDATA[a]b]]c]]]></r>',
        "cdata_long": '<r><![CDATA[' + 'x' * 1025 + ']]></r>',
        "text_brackets": '<r>a]b]]c]</r>',
        "text_before_bad_cdata": '<r>abc]]></r>',
        "prolog_space": ' \n<r/> \r\n<!--done--><?end?>',
        "bad_before_root": 'abc<r/>',
        "bad_after_root": '<r/> \n junk',
        "xml_version_11": '<?xml version="1.1"?><r/>',
        "xml_decl_empty": '<?xml?><r/>',
        "xml_decl_bad": '<?xml encoding="UTF-8"?><r/>',
        "xml_decl_duplicate": '<?xml version="1.0" version="1.0"?><r/>',
        "xml_decl_order": '<?xml version="1.0" standalone="yes" encoding="UTF-8"?><r/>',
        "xml_decl_spaces": ' <?xml version="1.0"?><r/>',
        "xml_decl_case": '<?XML version="1.0"?><r/>',
        "pi_newlines": '<?test a\r\nb\rc?><r/>',
        "comment_newlines": '<!--a\r\nb\rc--><r/>',
        "comment_empty": '<r><!----></r>',
        "comment_triple_dash": '<r><!--x---></r>',
        "unknown_declaration": '<!foo bar><r/>',
        "doctype_empty": '<!DOCTYPE><r/>',
        "doctype_missing_system": '<!DOCTYPE r SYSTEM><r/>',
        "doctype_bad_system": '<!DOCTYPE r SYSTEM xyz><r/>',
        "doctype_public": '<!DOCTYPE r PUBLIC "public id" "file"><r>&nbsp;</r>',
        "doctype_public_quote": '<!DOCTYPE r PUBLIC "public [id]" "file[1]"><r/>',
        "doctype_double": '<!DOCTYPE r><!DOCTYPE r><r/>',
        "doctype_comment_pi": '<!DOCTYPE r [<!--a--><?inside b?><!ELEMENT r EMPTY>]><r/>',
        "doctype_element_bad": '<!DOCTYPE r [<!ELEMENT r garbage>]><r/>',
        "doctype_element_nested": '<!DOCTYPE r [<!ELEMENT r ((a,b)*|c)+><!ELEMENT a EMPTY>]><r/>',
        "doctype_entity_bad": '<!DOCTYPE r [<!ENTITY foo garbage>]><r>&foo;</r>',
        "doctype_entity_quoted_angle": '<!DOCTYPE r [<!ENTITY foo "a>b">]><r>&foo;</r>',
        "doctype_entity_duplicate": '<!DOCTYPE r [<!ENTITY foo "a"><!ENTITY foo "b">]><r>&foo;</r>',
        "doctype_entity_unparsed": '<!DOCTYPE r [<!NOTATION n SYSTEM "x"><!ENTITY foo SYSTEM "x" NDATA n>]><r>&foo;</r>',
        "doctype_parameter": '<!DOCTYPE r [<!ENTITY % p "x">%p;]><r/>',
        "doctype_parameter_undeclared": '<!DOCTYPE r [%p;]><r/>',
        "attlist_enum": '<!DOCTYPE r [<!ATTLIST r a (x|y) " x  y " b CDATA #FIXED "v">]><r/>',
        "attlist_notation": '<!DOCTYPE r [<!ATTLIST r a NOTATION (x|y) #IMPLIED>]><r a=" x  y "/>',
        "attlist_unknown_type": '<!DOCTYPE r [<!ATTLIST r a WRONG "v">]><r/>',
        "attlist_id_default": '<!DOCTYPE r [<!ATTLIST r a ID "default">]><r/>',
        "attlist_duplicate": '<!DOCTYPE r [<!ATTLIST r a CDATA "first" a CDATA "second">]><r/>',
        "attlist_numeric_space": '<!DOCTYPE r [<!ATTLIST r a NMTOKENS #IMPLIED>]><r a=" a &#32;  b &#9; "/>',
        "attlist_undefined": '<!DOCTYPE r [<!ATTLIST r a CDATA "&absent;">]><r/>',
        "attribute_reference_bad": '<r a="&#xZZ;"/>',
        "attribute_reference_truncated": '<r a="&no"/>',
        "attribute_numeric_invalid": '<r a="&#0;"/>',
        "attribute_lt": '<r a="a<b"/>',
        "attribute_missing_space": '<r a="a"b="b"/>',
        "attribute_missing_equal": '<r a "a"/>',
        "attribute_unquoted": '<r a=a/>',
        "end_tag_space": '<r></r  >',
        "end_tag_attrs": '<r></r a="1">',
        "empty_tag_space": '<r / >',
        "colon_name": '<:r :a="x"><p::x/></:r>',
        "unicode_name": '<한글 é="x"/>',
        "astral_name": '<𐀀/>',
        "numeric_empty": '<r>&#;</r>',
        "numeric_hex_empty": '<r>&#x;</r>',
        "numeric_upper_x": '<r>&#X41;</r>',
        "numeric_bad_digit": '<r>&#12a;</r>',
        "entity_empty": '<r>&;</r>',
        "entity_bad_name": '<r>&1foo;</r>',
        "entity_space": '<r>&a b;</r>',
        "entity_amp": '<r>&a&b;</r>',
    }
    for name, text in extended.items():
        yield name, text.encode()
    for endian in ('le', 'be'):
        prefix = b'\xff\xfe' if endian == 'le' else b'\xfe\xff'
        body = '<r>'.encode('utf-16' + endian)
        yield 'lone_surrogate_' + endian, prefix + body + (b'\x00\xd8' if endian == 'le' else b'\xd8\x00')
    yield 'truncated_utf8', b'<r>\xf0\x9f'
    yield 'null_byte', b'<r>\x00</r>'
    source = b'<?xml version="1.0"?><r a="value"><p>abc&amp;def</p><![CDATA[x]]><!--c--></r>'
    for pos in (1, 5, 22, 28, 35, 40, 43, 50, 57, 66, 70):
        yield f"truncated_{pos}", source[:pos]


def prepare():
    corpus = ROOT / "corpus"
    corpus.mkdir(exist_ok=True)
    sources = {}
    for name, data in cases():
        filename = name + ".xml"
        (corpus / filename).write_bytes(data)
        sources[filename] = {"source": "handwritten", "sha256": hashlib.sha256(data).hexdigest()}
    for epub in sorted((REPO / "test/epubs").glob("*.epub")):
        with zipfile.ZipFile(epub) as archive:
            for member in archive.namelist():
                name = member.lower()
                if not name.endswith((".xml", ".ncx", ".xhtml", ".html")):
                    continue
                data = archive.read(member)
                if not (name in ("meta-inf/container.xml", "meta-inf/encryption.xml") or name.endswith(".ncx")
                        or b'<nav' in data):
                    continue
                filename = epub.stem + "--" + member.replace('/', '_')
                (corpus / filename).write_bytes(data)
                sources[filename] = {"source": str(epub.relative_to(REPO)) + ':' + member,
                                     "sha256": hashlib.sha256(data).hexdigest()}
    (ROOT / "sources.json").write_text(json.dumps(sources, indent=2, ensure_ascii=False) + '\n')


def capture(oracle, path, mode, count=0, stop=0):
    result = subprocess.run([str(oracle), str(path), mode, str(count), str(stop)],
                            check=True, capture_output=True, text=True)
    return [json.loads(line) for line in result.stdout.splitlines()]


def record(oracle, path):
    variants = []
    lookup = {}
    schedules = {}

    def add(name, events):
        indexes = []
        for event in events:
            key = json.dumps(event, sort_keys=True)
            if key not in lookup:
                lookup[key] = len(variants)
                variants.append(event)
            indexes.append(lookup[key])
        # Run-length encode schedule mappings, preserving every split's exact result.
        runs = []
        for index in indexes:
            if runs and runs[-1][1] == index:
                runs[-1][0] += 1
            else:
                runs.append([1, index])
        schedules[name] = runs

    add("splits", capture(oracle, path, "splits"))
    for size in (1, 7, 512, 1024):
        add(f"chunks_{size}", capture(oracle, path, "chunks", size))
    for stop in (1, 2, 3):
        add(f"stop_{stop}_splits", capture(oracle, path, "splits", stop=stop))
        for size in (1, 7, 512, 1024):
            add(f"stop_{stop}_chunks_{size}", capture(oracle, path, "chunks", size, stop))
    return {"variants": variants, "schedules": schedules}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("action", choices=("record", "verify"))
    args = parser.parse_args()
    if args.action == "record":
        prepare()
    sources = json.loads((ROOT / "sources.json").read_text())
    replays = 0
    for name, source in sources.items():
        path = ROOT / "corpus" / name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == source["sha256"], name
        result = record(args.oracle.resolve(), path)
        golden = ROOT / "goldens" / (name + ".json.gz")
        if args.action == "record":
            golden.write_bytes(gzip.compress((json.dumps(result, ensure_ascii=False, indent=2) + '\n').encode(), mtime=0))
        else:
            assert result == json.loads(gzip.decompress(golden.read_bytes())), f"Oracle drift: {name}"
        replays += sum(sum(count for count, _ in runs) for runs in result["schedules"].values())
    print(f"{args.action}: {len(sources)} inputs, {replays} schedules, exact event/status match")


if __name__ == "__main__":
    main()
