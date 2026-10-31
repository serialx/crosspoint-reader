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
  const std::string path = "test.zip";
  packageCaptureLogs = true;
  Storage.setFile("test.zip", std::vector<uint8_t>(21));
  {
    ZipFile zip(path);
    assert(!zip.loadZipDetails());
    expect("ERR|ZIP|File too small to be a valid zip");
  }
  Storage.setFile("test.zip", std::vector<uint8_t>(10));
  {
    ZipFile zip(path);
    assert(zip.open());
    zip.lastLookupName = "entry";
    zip.lastLookupValid = true;
    assert(zip.readFileToMemory("entry") == nullptr);
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
    ZipFile zip(path);
    ZipFile::FileStatSlim stat{};
    assert(!zip.loadFileStatSlim("abc", &stat));
    expect("ERR|ZIP|Failed to read entry name for abc: expected 3 bytes, got -1");
  }

  for (size_t length = 0; length < 30; ++length) {
    Storage.setFile(path, std::vector<uint8_t>(30));
    ZipFile zip(path);
    assert(zip.open());
    zip.lastLookupName = "entry";
    zip.lastLookupValid = true;
    HalFile::shortReadOffset = 0;
    HalFile::shortReadCount = length;
    assert(zip.readFileToMemory("entry") == nullptr);
    expect("ERR|ZIP|Something went wrong reading the local header");
  }
  Storage.setFile(path, std::vector<uint8_t>(30));
  {
    ZipFile zip(path);
    assert(zip.open());
    zip.lastLookupName = "entry";
    zip.lastLookupValid = true;
    assert(zip.readFileToMemory("entry") == nullptr);
    expect("ERR|ZIP|Not a valid zip file header");
  }
  Storage.setFile(path, bytes);
  ziptest::operations = 0;
  ziptest::fail = 4;
  {
    ZipFile zip(path);
    size_t size = 777;
    assert(!zip.getInflatedFileSize("abc", &size));
    expect("ERR|ZIP|Failed to seek central directory");
  }
  ziptest::fail = 0;
}
