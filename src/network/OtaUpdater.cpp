#include "OtaUpdater.h"

// clang-format off
// HttpDownloader must precede ESP headers (Arduino/SdFat and lwip macros).
#include "HttpDownloader.h"
#include <Logging.h>
#include <Memory.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
// clang-format on

#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"

namespace {
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/latest";
struct Context {
  std::string& latest;
  std::string& url;
  std::unique_ptr<ReleaseJsonParser> parser;
  const esp_partition_t* partition = nullptr;
  esp_ota_handle_t handle = 0;
  OtaUpdater::ProgressCallback progress = nullptr;
  void* progressContext = nullptr;
};
size_t operation(void* opaque, XpOtaOperation op, const uint8_t* bytes, size_t len) {
  auto& c = *static_cast<Context*>(opaque);
  switch (op) {
    case XP_OTA_PARSER:
      c.parser = makeUniqueNoThrow<ReleaseJsonParser>();
      return c.parser != nullptr;
    case XP_OTA_FEED:
      c.parser->feed(reinterpret_cast<const char*>(bytes), len);
      return 0;
    case XP_OTA_HAS_TAG:
      return c.parser->foundTag();
    case XP_OTA_HAS_FIRMWARE:
      return c.parser->foundFirmware();
    case XP_OTA_TAG:
      return reinterpret_cast<size_t>(c.parser->getTagName());
    case XP_OTA_URL:
      return reinterpret_cast<size_t>(c.parser->getFirmwareUrl());
    case XP_OTA_SIZE:
      return c.parser->getFirmwareSize();
    case XP_OTA_ASSET:
      c.parser->setFirmwareAssetName(reinterpret_cast<const char*>(bytes));
      return 0;
    case XP_OTA_STORE:
      c.latest = c.parser->getTagName();
      c.url = c.parser->getFirmwareUrl();
      return 0;
    case XP_OTA_PARTITION:
      c.partition = esp_ota_get_next_update_partition(nullptr);
      return c.partition != nullptr;
    case XP_OTA_BEGIN:
      return esp_ota_begin(c.partition, OTA_SIZE_UNKNOWN, &c.handle);
    case XP_OTA_WRITE:
      return esp_ota_write(c.handle, bytes, len);
    case XP_OTA_ABORT:
      return esp_ota_abort(c.handle);
    case XP_OTA_END:
      return esp_ota_end(c.handle);
    case XP_OTA_SELECT:
      return esp_ota_set_boot_partition(c.partition);
    case XP_OTA_CHIP:
      return firmware_flash::runningPartitionChipId();
    case XP_OTA_PROGRESS:
      c.progress(c.progressContext);
      return 0;
  }
  return 0;
}
bool fetch(void* opaque, bool release, XpOtaSink sink, void* ctx) {
  auto& c = *static_cast<Context*>(opaque);
  const std::function<bool(const uint8_t*, size_t)> callback = [=](const uint8_t* data, size_t len) {
    return sink(ctx, data, len);
  };
  if (release) return HttpDownloader::fetchUrl(latestReleaseUrl, callback);
  return HttpDownloader::fetchUrl(c.url, callback);
}
// Events 0..6 are release discovery; 7..15 are installation milestones/failures.
void diagnostic(void*, uint32_t event, size_t a, size_t b, const char* text) {
  switch (event) {
    case 0:
      LOG_ERR("OTA", "OOM: release parser");
      break;
    case 1:
      LOG_ERR("OTA", "Release check fetch failed");
      break;
    case 2:
      LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", a ? "yes" : "no", b ? "yes" : "no");
      break;
    case 3:
      LOG_ERR("OTA", "No tag_name in release JSON");
      break;
    case 4:
      LOG_INF("OTA", "No %s asset in latest release", text);
      break;
    case 5:
      LOG_DBG("OTA", "Found update: tag=%s size=%zu", text, a);
      break;
    case 6:
      LOG_DBG("OTA", "Firmware URL: %s", text);
      break;
    case 7:
      LOG_ERR("OTA", "No OTA partition available");
      break;
    case 8:
      LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(static_cast<esp_err_t>(a)));
      break;
    case 9:
      LOG_ERR("OTA", "wrong chip: image=0x%04X device=0x%04X", static_cast<unsigned>(a), static_cast<unsigned>(b));
      break;
    case 10:
      LOG_ERR("OTA", "wrong board: image=%s device=%.*s", text, static_cast<int>(board_tag::boardNameLen()),
              board_tag::boardName());
      break;
    case 11:
      LOG_ERR("OTA", "Firmware install aborted: wrong device");
      break;
    case 12:
      LOG_ERR("OTA", "Firmware install failed (%s)", a ? "download" : "flash write");
      break;
    case 13:
      LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(static_cast<esp_err_t>(a)));
      break;
    case 14:
      LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(static_cast<esp_err_t>(a)));
      break;
    case 15:
      LOG_INF("OTA", "Update completed");
      break;
  }
}
XpOtaServices services(Context& c) {
  return {&c,
          operation,
          fetch,
          diagnostic,
          reinterpret_cast<const uint8_t*>(board_tag::TAG),
          20 + board_tag::boardNameLen() + 1,
          c.progress != nullptr};
}
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  LOG_DBG("OTA", "Checking for update (current: %s)", CROSSPOINT_VERSION);
  Context ctx{latestVersion, otaUrl, nullptr};
  const auto api = services(ctx);
  return static_cast<OtaUpdaterError>(xp_ota_check(&state, &api));
}
bool OtaUpdater::isUpdateNewer() const {
  return xp_ota_newer(state.available, CROSSPOINT_VERSION, latestVersion.c_str());
}
const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }
OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback progress, void* opaque) {
  Context ctx{latestVersion, otaUrl, nullptr};
  ctx.progress = progress;
  ctx.progressContext = opaque;
  const auto api = services(ctx);
  return static_cast<OtaUpdaterError>(xp_ota_install(&state, &api, isUpdateNewer()));
}
