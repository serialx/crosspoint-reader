#include "Oracle.h"

#include <iostream>

#include "FirmwareFlasher.h"
#include "OtaBootSwitch.h"
#include "OtaUpdater.h"
namespace firmware_flash {
uint16_t runningPartitionChipId() {
  oracle::record("chip");
  return oracle::fault == "unknown-chip" ? 0xffff : 5;
}
}  // namespace firmware_flash
static void dump(const std::string& name) {
  std::cout << "CASE " << name << '\n';
  for (const auto& line : oracle::trace) std::cout << line << '\n';
}
static void prepare(const std::string& version = "2.1.0", bool asset = true) {
  oracle::image.assign(96, 0);
  oracle::image[0] = 0xe9;
  oracle::image[12] = 5;
  oracle::manifest = "{\"tag_name\":\"" + version + "\",\"assets\":[{\"name\":\"crosspoint-" + version +
                     (asset ? "-x3-x4.bin" : "-x4pro.bin") +
                     "\",\"size\":96,\"browser_download_url\":\"https://fw\"}]}";
}
static void install(const std::string& fault, size_t chunk, int cut = -1) {
  oracle::reset();
  prepare();
  oracle::fault = fault;
  oracle::chunk = chunk;
  if (fault == "wrong-chip") oracle::image[12] = 9;
  if (fault == "wrong-board") memcpy(oracle::image.data() + 30, "CROSSPOINT-BOARD-V1:sticky;", 25);
  if (fault == "same-board") memcpy(oracle::image.data() + 30, "CROSSPOINT-BOARD-V1:x4;", 21);
  if (fault == "truncated") oracle::image.resize(13);
  if (fault == "empty") oracle::image.clear();
  if (fault == "zero-size" || fault == "large-size") {
    auto pos = oracle::manifest.find("\"size\":96");
    oracle::manifest.replace(pos, 9, fault == "zero-size" ? "\"size\":0" : "\"size\":1000");
  }
  OtaUpdater updater;
  auto checked = updater.checkForUpdate();
  oracle::record("checked %d newer %d size %zu", checked, updater.isUpdateNewer(), updater.getOtaSize());
  oracle::step = 0;
  oracle::cut = cut;
  try {
    auto r = updater.installUpdate(
        [](void* p) {
          auto& u = *static_cast<OtaUpdater*>(p);
          oracle::record("progress %zu %zu", u.getProcessedSize(), u.getTotalSize());
        },
        &updater);
    oracle::record("result %d", r);
  } catch (const std::runtime_error&) {
    oracle::record("power-loss");
  }
  oracle::record("state %zu %d %d", updater.getProcessedSize(), oracle::verified, oracle::selected);
  if (oracle::selected && !oracle::verified) std::abort();
  dump("install-" + fault + "-" + std::to_string(chunk) + "-" + std::to_string(cut));
}
int main(int argc, char** argv) {
  if (argc == 3) {
    int cut = std::stoi(argv[2]);
    if (std::string(argv[1]) == "install-power") {
      install("", 31, cut);
      return 0;
    }
    oracle::reset();
    oracle::otadata.fill(255);
    ota_boot::SelectEntry a{};
    a.ota_seq = 8;
    a.crc = ota_boot::computeSeqCrc(8);
    memcpy(oracle::otadata.data(), &a, 32);
    oracle::cut = cut;
    oraclePartition.subtype = 16;
    oracle::record("result %d", ota_boot::switchTo(&oraclePartition));
    if (memcmp(oracle::otadata.data(), &a, 32)) std::abort();
    dump("boot-power-complete");
    return 0;
  }

  for (const char* v : {"2.1.0", "1.2.3", "1.2.4", "1.3.0", "1.1.9", "0.9.9", "1.2.3-rc1", "1.2.3+build", " 2.0.0",
                        "+2.0.0", "-1.2.3", "2", "1.3", "2.0.0rc", "1.6.5rc", "1.5.0-rc-4", "1.2.3-dev-branch-sha"}) {
    oracle::reset();
    prepare(v);
    OtaUpdater u;
    auto r = u.checkForUpdate();
    oracle::record("manifest %d %d %s %zu %zu", r, u.isUpdateNewer(), u.getLatestVersion().c_str(), u.getOtaSize(),
                   u.getTotalSize());
    dump(std::string("version-") + v);
  }
  for (const char* fault : {"oom", "manifest-fetch", "no-tag", "no-asset"}) {
    oracle::reset();
    prepare("2.1.0", std::string(fault) != "no-asset");
    oracle::fault = fault;
    if (oracle::fault == "no-tag") oracle::manifest = "{}";
    OtaUpdater u;
    auto r = u.checkForUpdate();
    oracle::record("result %d", r);
    dump(std::string("manifest-") + fault);
  }
  for (const char* fault :
       {"", "partition", "begin", "fetch", "write", "verify", "select", "abort", "wrong-chip", "wrong-board",
        "same-board", "unknown-chip", "truncated", "empty", "zero-size", "large-size", "write-after"})
    for (size_t chunk : {1, 7, 13, 14, 31, 96}) install(fault, chunk);
  for (const char* version : {"1.2.3", "0.9.0"}) {
    oracle::reset();
    prepare(version);
    OtaUpdater updater;
    updater.checkForUpdate();
    oracle::record("install %d", updater.installUpdate());
    dump(std::string("skip-") + version);
  }
  oracle::reset();
  {
    OtaUpdater updater;
    oracle::record("install %d", updater.installUpdate());
    dump("skip-unchecked");
  }
  oracle::reset();
  prepare();
  {
    OtaUpdater updater;
    updater.checkForUpdate();
    oracle::fault = "manifest-fetch";
    oracle::record("check-again %d", updater.checkForUpdate());
    oracle::fault = "";
    oracle::record("retained %d %zu", updater.isUpdateNewer(), updater.getOtaSize());
    oracle::record("install %d", updater.installUpdate());
    dump("retry-null-progress");
  }
  oracle::reset();
  oracle::record("null-boot %d", ota_boot::switchTo(nullptr));
  dump("null-boot");
  for (int variant = 0; variant < 8; ++variant)
    for (const char* fault : {"", "find", "read", "read-second", "erase", "partition-write"}) {
      oracle::reset();
      oracle::fault = fault;
      oracle::otadata.fill(255);
      oracleData.size = variant == 7 ? 4096 : 8192;
      ota_boot::SelectEntry a{};
      a.ota_seq = variant == 6 ? 0xfffffffe : 7;
      a.ota_state = variant == 3 ? 3 : 0;
      a.crc = ota_boot::computeSeqCrc(a.ota_seq);
      if (variant) memcpy(oracle::otadata.data(), &a, 32);
      a.ota_seq = 10;
      a.ota_state = variant == 4 ? 4 : 0;
      a.crc = ota_boot::computeSeqCrc(a.ota_seq) + (variant == 5);
      if (variant > 1 && variant != 6) memcpy(oracle::otadata.data() + 4096, &a, 32);
      oraclePartition.subtype = variant == 5 ? 0 : 17;
      auto result = ota_boot::switchTo(&oraclePartition);
      oracle::record("result %d data %u", result, oracle::hash(oracle::otadata.data(), 8192));
      dump("boot-" + std::to_string(variant) + "-" + fault);
    }
  std::vector<std::string> versions = {
#include "versions.inc"
  };
  for (const auto& current : versions)
    for (const auto& latest : versions) {
      int cm = 0, cn = 0, cp = 0, lm = 0, ln = 0, lp = 0;
      int c = sscanf(current.c_str(), "%d.%d.%d", &cm, &cn, &cp), l = sscanf(latest.c_str(), "%d.%d.%d", &lm, &ln, &lp);
      // Exclude only cases where the original comparison reads an uninitialized component.
      bool defined = current == latest || latest.empty() ||
                     (c >= 1 && l >= 1 && (cm != lm || (c >= 2 && l >= 2 && (cn != ln || (c >= 3 && l >= 3)))));
      if (!defined) continue;
      oracle::reset();
      oracle::currentVersion = current;
      prepare(latest);
      OtaUpdater u;
      u.checkForUpdate();
      std::cout << "VERSION " << current << " | " << latest << " | " << u.isUpdateNewer() << '\n';
    }
}
