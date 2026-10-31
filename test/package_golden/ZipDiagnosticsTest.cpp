#include <HalStorage.h>
#include <Logging.h>
#include <ZipFile.h>

#include <cassert>
#include <string>

static void expect(const std::string& text) {
  assert(packageLogs.size() == 1 && packageLogs.front() == text);
  packageLogs.clear();
}
int main() {
  packageCaptureLogs = true;
  Storage.setFile("test.zip", std::vector<uint8_t>(21));
  {
    ZipFile zip("test.zip");
    assert(!zip.loadZipDetails());
    expect("ERR|ZIP|File too small to be a valid zip");
  }
  Storage.setFile("test.zip", std::vector<uint8_t>(10));
  {
    ZipFile zip("test.zip");
    ZipFile::FileStatSlim stat{};
    assert(zip.getDataOffset(stat) == -1);
    expect("ERR|ZIP|Something went wrong reading the local header");
  }
  std::vector<uint8_t> bytes(71);
  bytes[0] = 0x50;
  bytes[1] = 0x4b;
  bytes[2] = 1;
  bytes[3] = 2;
  bytes[28] = 3;
  bytes[49] = 0x50;
  bytes[50] = 0x4b;
  bytes[51] = 5;
  bytes[52] = 6;
  bytes[59] = 1;
  Storage.setFile("test.zip", bytes);
  HalFile::failReadOffset = 46;
  {
    ZipFile zip("test.zip");
    ZipFile::FileStatSlim stat{};
    assert(!zip.loadFileStatSlim("abc", &stat));
    expect("ERR|ZIP|Failed to read entry name for abc: expected 3 bytes, got -1");
  }
}
