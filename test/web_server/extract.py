#!/usr/bin/env python3
"""Compile unchanged server methods against deterministic host transport/storage leaves."""

import argparse
import gzip
import json
import os
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]

def function(source, name):
    pattern = r"^[^\n;{}]*\b" + re.escape(name) + r"\([^{}]*?\{"
    match = re.search(pattern, source, re.MULTILINE)
    if not match:
        raise ValueError(f"function not found: {name}")
    # Every selected definition ends at column zero; nested blocks are indented.
    line_end = source.find("\n", match.end())
    if "}" in source[match.end():line_end]:
        return source[match.start():line_end]
    end = source.find("\n}", match.end())
    return source[match.start():end + 2]


def generate(source, header):
    # Match ESP32 unsigned-long clock arithmetic in the pre-port fetch oracle.
    source = source.replace("const unsigned long fetchStartedAt", "const uint32_t fetchStartedAt")
    source = source.replace("unsigned long lastBrowserHeartbeat", "uint32_t lastBrowserHeartbeat")
    source = source.replace("const unsigned long now = millis();", "const uint32_t now = millis();")
    source = source.replace("millis() - fetchStartedAt", "static_cast<unsigned long>(uint32_t(millis() - fetchStartedAt))")
    source = source.replace("void CrossPointWebServer::resumeTransferServices() {",
                            "void CrossPointWebServer::resumeTransferServices() {\n  if (host::beforeResume) host::beforeResume();")
    source = re.sub(r"\bJsonDocument\b", "WebJsonDocument", source)
    if "xp_web_files" in source and 'return {nullptr, 0};\n      }\n      if (!*key)' not in source:
        raise ValueError("argument release hook changed: review String lifetime instrumentation")
    source = source.replace('return {nullptr, 0};\n      }\n      if (!*key)',
        'if (std::getenv("WEB_STRING_PARSE_CHECK") && b.argument.capacity() > std::string{}.capacity()) { '
        'std::cerr << "String scratch retained at parse completion: " << b.argument.capacity() << " B\\n"; std::abort(); }\n'
        'return {nullptr, 0};\n      }\n      if (!*key)')
    header = re.sub(r"\bJsonDocument\b", "WebJsonDocument", header)
    header = re.sub(r"^#(?:pragma|include).*\n", "", header, flags=re.MULTILINE)
    header = header.replace(" private:", " public:")
    header = header.replace(": private XpWebLifecycle", ": public XpWebLifecycle")
    pieces = ['#include "Host.h"', '#include "Endpoints.h"', header]
    if "xp_web_files" in source:
        pieces.insert(0, '#include "xp_web.h"')
    helpers = (ROOT / "lib/FsHelpers/FsHelpers.cpp").read_text()
    pieces.append("namespace FsHelpers {\n" + "\n".join(function(helpers, name) for name in
                  ["normalisePath", "isSafePathComponent", "checkFileExtension"]) + "\n}")
    for include in re.findall(r'^#include "(html/[^"\n]+)"', source, re.MULTILINE):
        pieces.append('#include "src/network/' + include + '"')
    pieces.append(re.sub(r'^#include[^\n]*\n', '', source, flags=re.MULTILINE))
    if "XpWebWsUpload wsPolicy" in source:
        for old, field in {
            "wsUploadSize": "total", "wsUploadReceived": "received",
            "wsUploadStartTime": "started_at", "wsUploadInProgress": "active",
            "wsUploadClientNum": "client", "wsLastProgressSent": "last_progress",
            "wsLastCompleteSize": "complete_size", "wsLastCompleteAt": "completed_at",
        }.items():
            pieces.append(f"#define {old} wsPolicy.{field}")
    pieces.append('#include "Cases.h"')
    return "\n\n".join(pieces) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, required=True)
    parser.add_argument("--arduinojson", type=pathlib.Path, required=True)
    parser.add_argument("--source", type=pathlib.Path, default=ROOT / "src/network/CrossPointWebServer.cpp")
    parser.add_argument("--header", type=pathlib.Path, default=ROOT / "src/network/CrossPointWebServer.h")
    parser.add_argument("--record", action="store_true")
    parser.add_argument("--mutation-check", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--archive", type=pathlib.Path)
    parser.add_argument("--ffi-include", type=pathlib.Path, default=ROOT / "rust/crates/xp-ffi/include")
    parser.add_argument("--fault-check", action="store_true")
    parser.add_argument("--review-check", action="store_true")
    args = parser.parse_args()
    args.build.mkdir(parents=True, exist_ok=True)
    generated = args.build / "server.cpp"
    source = args.source.read_text()
    header = args.header.read_text()

    def run(text):
        generated.write_text(generate(text, header))
        instrumentation = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []
        link = [str(ROOT / "test/cmake/xp_ffi_host.cpp"), str(args.archive)] if args.archive else []
        if args.archive:
            instrumentation += ["-DXP_FFI_HOST_FAULT_INJECTION"]
        subprocess.run(["c++", "-std=c++20", "-funsigned-char", "-O1", "-g", *instrumentation, "-I" + str(ROOT / "test/web_server/stubs"),
                        "-I" + str(ROOT / "test/web_server"), "-I" + str(ROOT),
                        "-I" + str(args.arduinojson), "-I" + str(args.ffi_include),
                        str(generated), *link, "-o", str(args.build / "oracle")], check=True)
        return subprocess.check_output([str(args.build / "oracle")])

    actual = run(source)
    (args.build / "actual.jsonl").write_bytes(actual)
    golden = ROOT / "test/web_server/golden.jsonl.gz"
    if args.record:
        golden.write_bytes(gzip.compress(actual, mtime=0))
    expected = gzip.decompress(golden.read_bytes())
    if actual != expected:
        old = [json.loads(line) for line in expected.splitlines()]
        new = [json.loads(line) for line in actual.splitlines()]
        failures = [b["case"] for a, b in zip(old, new) if a != b]
        raise SystemExit(f"web oracle mismatch: {failures}; expected {len(old)} cases, got {len(new)}")
    print(f"web oracle: {len(actual.splitlines())} cases match byte for byte")
    guard = subprocess.run([str(args.build / "oracle")], env={**os.environ, "WEB_TRACE_LIMIT_CHECK": "1"},
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if guard.returncode == 0 or b"transcript string limit:" not in guard.stderr:
        raise SystemExit("transcript string-limit guard did not reject an oversized event")
    print("transcript string-limit guard: passed")
    if args.review_check:
        if not args.archive:
            raise SystemExit("--review-check requires --archive")
        for variable in ["WEB_COMPLETION_CHECK", "WEB_LOCALIZATION_CHECK", "WEB_SYSTEM_NAME_CHECK", "WEB_KEY_LIFETIME_CHECK", "WEB_JOB_ALLOCATION_CHECK", "WEB_FETCH_ALLOCATION_CHECK"]:
            subprocess.run([str(args.build / "oracle")], env={**os.environ, variable: "1"},
                           stdout=subprocess.DEVNULL, check=True)
        for stage in ["tls", "pkcs12", "resume", "fetch"]:
            subprocess.run([str(args.build / "oracle")], env={**os.environ, "WEB_STRING_LIFETIME_CHECK": stage},
                           stdout=subprocess.DEVNULL, check=True)
        subprocess.run([str(args.build / "oracle")], env={**os.environ, "WEB_STRING_PARSE_CHECK": "1"},
                       stdout=subprocess.DEVNULL, check=True)
        print("review checks: completion consistency, translated allocation errors, system-name listing and book-key body lifetime and allocation-free job routes passed")
    if args.fault_check:
        if not args.archive:
            raise SystemExit("--fault-check requires --archive")
        zero = subprocess.check_output([str(args.build / "oracle")], env={**os.environ, "WEB_FAIL_AFTER": "0"})
        def jobs(data):
            return [row for line in data.splitlines() if (row := json.loads(line))["case"].startswith("job")]
        if jobs(zero) != jobs(expected):
            raise SystemExit("job transcripts changed with Rust allocator disabled")
        def rpc(data):
            return [row for line in data.splitlines() if (row := json.loads(line))["case"].startswith(("crypto", "relay", "rc-json", "fetch"))]
        if rpc(zero) != rpc(expected):
            raise SystemExit("relay/crypto transcripts changed with Rust allocator disabled")
        for count in range(1, 128):
            subprocess.run([str(args.build / "oracle")], env={**os.environ, "WEB_FAIL_AFTER": str(count)},
                           stdout=subprocess.DEVNULL, check=True)
        print("allocation failures: 128 budgets across all cases, no retained Rust allocation or WS owner; job transcripts identical with allocator disabled")
    if args.mutation_check:
        changed = source.replace('"Missing path"', '"Missing PATH"', 1)
        if changed == source:
            changed = source.replace('WebFileBridge::route(*this, XP_WEB_DOWNLOAD)',
                                     'WebFileBridge::route(*this, XP_WEB_DELETE)', 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed download error")
        print("mutation check: rejected changed download response")
        changed = source.replace('"Missing JSON body"', '"Missing JSON BODY"', 1)
        if "xp_web_config" in source:
            changed = source.replace('XP_WEB_SETTINGS_POST);', 'XP_WEB_SETTINGS_GET);', 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed settings response")
        print("mutation check: rejected changed settings response")
        changed = source.replace('"Invalid font file"', '"Invalid FONT file"', 1)
        if changed == source:
            changed = source.replace('fontUpload.valid = true;', 'fontUpload.valid = false;', 1)
        if changed == source:
            changed = source.replace('WebFontBridge::route(*this, XP_WEB_FONT_LIST)',
                                     'WebFontBridge::route(*this, XP_WEB_FONT_DELETE)', 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed fonts behavior")
        print("mutation check: rejected changed fonts behavior")
        changed = source.replace('now - job.updatedAt > PLUGIN_JOB_LEASE_MS',
                                 'now - job.updatedAt >= PLUGIN_JOB_LEASE_MS', 1)
        if changed == source:
            changed = source.replace('XP_WEB_JOB_CLAIM);', 'XP_WEB_JOB_STATUS);', 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed job lease policy")
        print("mutation check: rejected changed job lease policy")
        changed = source.replace('"bad plugin path"', '"BAD plugin path"', 1)
        if changed == source:
            changed = source.replace('XP_WEB_PLUGIN_FILE);', 'XP_WEB_PLUGIN_LIST);', 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed plugin behavior")
        print("mutation check: rejected changed plugin behavior")
        changed = source.replace('static constexpr size_t RELAY_BODY_LIMIT = 32 * 1024;',
                                 'static constexpr size_t RELAY_BODY_LIMIT = 31 * 1024;', 1)
        if changed == source:
            changed = source.replace('WebRelayBridge bridge(*this);', 'return; WebRelayBridge bridge(*this);', 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed relay policy")
        print("mutation check: rejected changed relay policy")
        changed = source.replace('static constexpr int MAX_RANDOM_BYTES = 4096;',
                                 'static constexpr int MAX_RANDOM_BYTES = 4095;', 1)
        if changed == source:
            changed = source.replace('WebCryptoBridge bridge(*this);', 'return; WebCryptoBridge bridge(*this);', 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed crypto policy")
        print("mutation check: rejected changed crypto policy")
        changed = source.replace('segmentLimit > 0 && written - requestedOffset >= segmentLimit',
                                 'segmentLimit > 0 && written - requestedOffset > segmentLimit', 1)
        if changed == source:
            changed = source.replace('xp_web_fetch(static_cast<WebFileBridge*>(&bridge), &WebFetchBridge::services);',
                                     'return;', 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed fetch policy")
        print("mutation check: rejected changed fetch policy")
        changed = source.replace("delay(20);", "delay(21);", 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed stop ordering delay")
        print("mutation check: rejected changed stop delay")
        changed = source.replace('doc["mode"] = apMode ? "AP" : "STA";',
                                 'doc["mode"] = apMode ? "STA" : "AP";', 1)
        if changed == source:
            changed = source.replace('&WebStatusBridge::services, apMode);',
                                     '&WebStatusBridge::services, !apMode);', 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed status mode")
        print("mutation check: rejected changed status mode")
        changed = source.replace('HomePageHtml, sizeof(HomePageHtml), HomePageHtmlETag',
                                 'FilesPageHtml, sizeof(FilesPageHtml), FilesPageHtmlETag', 1)
        if changed == source:
            changed = source.replace('WebFileBridge::page(*this, XP_WEB_PAGE_HOME);',
                                     'WebFileBridge::page(*this, XP_WEB_PAGE_FILES);', 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed static resource")
        print("mutation check: rejected changed static resource")
        signature = "void CrossPointWebServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {"
        changed = source.replace(signature, signature + "\n  if (type == WStype_DISCONNECTED) return;", 1)
        if changed == source:
            raise SystemExit("mutation check did not find disconnect entry point")
        try:
            accepted = run(changed) == expected
        except subprocess.CalledProcessError as error:
            if error.cmd != [str(args.build / "oracle")]:
                raise
            accepted = False  # A failed ownership invariant also rejects this mutation.
        if accepted:
            raise SystemExit("mutation check did not reject ignored synchronous disconnect")
        generated.write_text(generate(source, header))
        print("mutation check: rejected ignored synchronous disconnect")


if __name__ == "__main__":
    main()
