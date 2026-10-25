#!/usr/bin/env python3
"""Compare complete production Section files against the captured C++ oracle."""

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import posixpath
import struct
import subprocess
import sys
import tarfile
import tempfile
from urllib.parse import unquote
import xml.etree.ElementTree as ET
from zipfile import ZipFile


HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent


def resolve(base, href):
    path, _, anchor = unquote(href).partition("#")
    return posixpath.normpath(posixpath.join(posixpath.dirname(base), path)), anchor


def prepare(epub, destination):
    with ZipFile(epub) as archive:
        for name in archive.namelist():
            path = PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts:
                raise ValueError(f"Unsafe ZIP member: {name}")
        archive.extractall(destination / "book")
        container = ET.fromstring(archive.read("META-INF/container.xml"))
        opf = container.find(".//{*}rootfile").attrib["full-path"]
        package = ET.fromstring(archive.read(opf))
        items = {item.attrib["id"]: item.attrib for item in package.findall("{*}manifest/{*}item")}
        spine_element = package.find("{*}spine")
        spine = []
        for itemref in spine_element:
            item = items[itemref.attrib["idref"]]
            if item["media-type"] != "application/xhtml+xml":
                raise ValueError(f"Unsupported spine type in {epub}: {item}")
            spine.append(resolve(opf, item["href"])[0])
        css = [resolve(opf, item["href"])[0] for item in items.values() if item["media-type"] == "text/css"]
        language = package.findtext("{*}metadata/{*}language", "")
        toc = []
        ncx_id = spine_element.get("toc")
        if ncx_id:
            ncx_path = resolve(opf, items[ncx_id]["href"])[0]
            ncx = ET.fromstring(archive.read(ncx_path))
            toc = [resolve(ncx_path, node.attrib["src"]) for node in ncx.findall(".//{*}navPoint/{*}content")]
        else:
            nav_item = next((item for item in items.values() if "nav" in item.get("properties", "").split()), None)
            if nav_item:
                nav_path = resolve(opf, nav_item["href"])[0]
                nav = ET.fromstring(archive.read(nav_path))
                for element in nav.findall(".//{*}nav"):
                    if "toc" in element.get("{http://www.idpf.org/2007/ops}type", "").split():
                        toc = [resolve(nav_path, node.attrib["href"]) for node in element.findall(".//{*}a")]
                        break
        lines = [f"language\t{language}"]
        lines.extend(f"spine\t{path}" for path in spine)
        lines.extend(f"css\t{path}" for path in css)
        lines.extend(f"toc\t{spine.index(path)}\t{anchor}" for path, anchor in toc if path in spine)
        if any("\n" in line or "\r" in line for line in lines):
            raise ValueError("Fixture metadata contains line breaks")
        (destination / "manifest.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
        return spine


def collect(dump):
    sections = {}
    inventory = []
    for epub in sorted((REPO / "test/epubs").glob("*.epub")):
        with tempfile.TemporaryDirectory(prefix="crosspoint-section-") as temporary:
            root = Path(temporary)
            spine = prepare(epub, root)
            result = subprocess.run([str(dump), str(root)], text=True, capture_output=True, check=True)
            records = [line.split("\t") for line in result.stdout.splitlines()]
            if len(records) != len(spine) * 3:
                raise ValueError(f"Missing sections for {epub.name}: {result.stdout}")
            for config, index, pages, size in records:
                name = f"{epub.stem}/{config}/{index}.bin"
                data = (root / "output" / config / f"{index}.bin").read_bytes()
                if len(data) != int(size):
                    raise ValueError(f"Short section: {name}")
                sections[name] = data
                inventory.append({"section": name, "spine": spine[int(index)], "pages": int(pages),
                                  "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
            print(f"{epub.name}: {len(spine)} spine items, {len(records)} sections")
    return sections, inventory


def write_archive(path, sections):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for name, data in sorted(sections.items()):
            entry = tarfile.TarInfo(name)
            entry.size = len(data)
            archive.addfile(entry, io.BytesIO(data))
    path.write_bytes(gzip.compress(buffer.getvalue(), compresslevel=9, mtime=0))


def read_archive(path):
    with tarfile.open(path, "r:gz") as archive:
        return {entry.name: archive.extractfile(entry).read() for entry in archive if entry.isfile()}


def compare(expected, actual):
    if expected.keys() != actual.keys():
        raise ValueError(f"Section set changed: missing={sorted(expected.keys() - actual.keys())}, "
                         f"added={sorted(actual.keys() - expected.keys())}")
    for name in sorted(expected):
        old, new = expected[name], actual[name]
        if old != new:
            offset = next((i for i, (a, b) in enumerate(zip(old, new)) if a != b), min(len(old), len(new)))
            raise ValueError(f"Section differs: {name}, first byte offset {offset}, "
                             f"expected {len(old)} bytes, got {len(new)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", type=Path, required=True)
    parser.add_argument("--goldens", type=Path, default=HERE / "goldens")
    parser.add_argument("--capture-baseline", action="store_true", help="Capture CURRENT C++ before porting; never use to fix a mismatch")
    args = parser.parse_args()
    if sys.byteorder != "little" or struct.calcsize("i") != 4:
        raise ValueError("The Section wire oracle requires a little-endian host with 32-bit int")
    if args.capture_baseline:
        subprocess.run(["git", "diff", "--exit-code", "HEAD", "--", "lib", "src", "rust"],
                       cwd=REPO, check=True, capture_output=True, text=True)
        if (args.goldens / "sections.tar.gz").exists():
            raise ValueError("Refusing to overwrite an existing baseline; use a new --goldens directory")
    sections, inventory = collect(args.dump.resolve())
    archive = args.goldens / "sections.tar.gz"
    fixtures = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted((REPO / "test/epubs").glob("*.epub"))}
    if args.capture_baseline:
        args.goldens.mkdir(parents=True, exist_ok=True)
        write_archive(archive, sections)
        if archive.stat().st_size >= 1_000_000:
            raise ValueError(f"Golden archive exceeds 1 MB: {archive.stat().st_size} bytes")
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip()
        manifest = {"baseline_revision": revision, "epubs": fixtures, "sections": inventory}
        (args.goldens / "inventory.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"Captured {len(sections)} sections, {sum(len(data) for data in sections.values())} raw bytes, "
              f"{archive.stat().st_size} gzip bytes")
    else:
        manifest = json.loads((args.goldens / "inventory.json").read_text(encoding="utf-8"))
        if fixtures != manifest["epubs"]:
            raise ValueError("EPUB corpus changed; inspect fixture changes before capturing a new baseline")
        compare(read_archive(archive), sections)
        if inventory != manifest["sections"]:
            raise ValueError("Section inventory changed")
        print(f"PASS: {len(sections)} complete sections are byte-identical")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(error.stdout or "", file=sys.stderr)
        print(error.stderr or "", file=sys.stderr)
        raise SystemExit(f"Section tool failed with exit status {error.returncode}")
    except ValueError as error:
        raise SystemExit(str(error))
