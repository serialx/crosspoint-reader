"""Run the production fetch handler against the pinned SDK and its offline transport."""
import argparse
from pathlib import Path
import subprocess
import sys

from extract import generate

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "freeink-sdk/libs/network/SecureNet"
parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--arduinojson", type=Path, required=True)
parser.add_argument("--html-root", type=Path, required=True)
args = parser.parse_args()
args.build.mkdir(parents=True, exist_ok=True)

# Reuse the SDK's offline transport and its SecureClient mock.
fixture = (SDK / "test/host/test_bulk_request.cpp").read_text()
setup, separator, _ = fixture.partition("int main() {")
if not separator:
    raise RuntimeError("SDK host fixture changed")
source = args.build / "fetch_retry.cpp"
# Keep the SDK's tiny board stubs distinct from the web oracle's richer doubles.
aliases = ("freeink", "String", "IPAddress", "millis", "delay", "WiFiClient", "Client", "base64")
prefix = "".join(f"#define {name} sdk_{name}\n" for name in aliases)
suffix = "".join(f"#undef {name}\n" for name in aliases)
server = ROOT / "src/network/CrossPointWebServer.cpp"
transport = server.read_text()
for name in ("SecureHttpClient", "FetchOptions", "FetchSink", "fetchResumable"):
    transport = transport.replace(f"freeink::{name}", f"sdk_freeink::{name}")
app = generate(transport, server.with_suffix(".h").read_text())
source.write_text(prefix + setup + suffix + "#define main oracle_main\n" + app + r"""
#undef main
int main() {
  for (bool preconnect : {false, true}) {
    record("sdk-fetch-post", [&](auto& app) {
      reset({"incomplete status", "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nbook"});
      std::string json = R"({"url":"https://example.test/catalog","dest":"/books/new.epub","method":"POST","body":"a\u0000b")";
      if (preconnect) json += R"(,"headers":{"X-App":"from-handler"})";
      app.server->args["plain"] = json + "}";
      app.registerRoutes();
      app.server->routes.at("1:/api/fetch")();
      const auto& requests = FakeNet::requests();
      assert(requests.size() == (preconnect ? 2 : 1));
      for (const auto& wire : requests) {
        assert(wire.starts_with("POST /catalog HTTP/1.1\r\n"));
        assert(wire.find("\r\nHost: example.test\r\n") != std::string::npos);
        assert(wire.find("\r\nContent-Length: 3\r\n") != std::string::npos);
        assert((wire.find("\r\nX-App: from-handler\r\n") != std::string::npos) == preconnect);
        assert(wire.substr(wire.find("\r\n\r\n") + 4) == std::string("a\0b", 3));
      }
      if (preconnect) {
        assert(requests[0] == requests[1]);
        assert(host::files.at("/books/new.epub")->bytes == "book");
      } else {
        assert(!host::files.contains("/books/new.epub"));
      }
    });
  }
}
""")
sys.path.insert(0, str(ROOT / "test"))
from rust_host import rust_archive

includes = [SDK / "test/host/stubs", SDK / "include", ROOT / "test/web_server/stubs",
            ROOT / "test/web_server", args.html_root, ROOT, args.arduinojson,
            ROOT / "rust/crates/xp-ffi/include"]
binary = args.build / "fetch_retry"
subprocess.run(["c++", "-std=c++20", "-funsigned-char", "-DFREEINK_NET_WOLFSSL=1",
                *["-I" + str(path) for path in includes], str(source),
                str(ROOT / "freeink-sdk/libs/font/FreeInkFont/third_party/freetype/src/base/md5.c"),
                str(ROOT / "test/cmake/xp_ffi_host.cpp"), rust_archive(catalog=False),
                "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
