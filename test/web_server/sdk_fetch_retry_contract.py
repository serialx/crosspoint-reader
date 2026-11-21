"""Measure the pinned SDK's retry classification with a read-only host request."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "freeink-sdk/libs/network/SecureNet"
parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, required=True)
args = parser.parse_args()
args.build.mkdir(parents=True, exist_ok=True)

# Reuse the SDK's offline transport and its SecureClient mock.
fixture = (SDK / "test/host/test_bulk_request.cpp").read_text()
setup, separator, _ = fixture.partition("int main() {")
if not separator:
    raise RuntimeError("SDK host fixture changed")
source = args.build / "fetch_retry.cpp"
source.write_text(setup + r"""
int main() {
  for (bool preconnect : {false, true}) {
    reset({"incomplete status", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"});
    freeink::SecureHttpClient client;
    assert(client.begin("https://example.test/catalog"));
    if (preconnect) assert(client.connect());
    client.addHeader("Accept", "application/json");
    const int status = client.GET();
    assert(status == (preconnect ? 200 : -1));
    assert(FakeNet::requests().size() == (preconnect ? 2 : 1));
  }
}
""")
binary = args.build / "fetch_retry"
subprocess.run(["c++", "-std=c++17", "-DFREEINK_NET_WOLFSSL=1",
                "-I" + str(SDK / "test/host/stubs"), "-I" + str(SDK / "include"),
                str(source), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
