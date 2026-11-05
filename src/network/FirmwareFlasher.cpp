#include "FirmwareFlasher.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <xp_ffi.h>

#include <memory>

#include "FirmwareBoardTag.h"
#include "OtaBootSwitch.h"

namespace firmware_flash {
const char* resultName(Result r) {
  switch (r) {
    case Result::OK:
      return "OK";
    case Result::OPEN_FAIL:
      return "OPEN_FAIL";
    case Result::TOO_SMALL:
      return "TOO_SMALL";
    case Result::TOO_LARGE:
      return "TOO_LARGE";
    case Result::BAD_MAGIC:
      return "BAD_MAGIC";
    case Result::BAD_SEGMENTS:
      return "BAD_SEGMENTS";
    case Result::BAD_CHECKSUM:
      return "BAD_CHECKSUM";
    case Result::BAD_SHA:
      return "BAD_SHA";
    case Result::BAD_CHIP:
      return "BAD_CHIP";
    case Result::WRONG_BOARD:
      return "WRONG_BOARD";
    case Result::BAD_SIZE:
      return "BAD_SIZE";
    case Result::NO_PARTITION:
      return "NO_PARTITION";
    case Result::OOM:
      return "OOM";
    case Result::READ_FAIL:
      return "READ_FAIL";
    case Result::ERASE_FAIL:
      return "ERASE_FAIL";
    case Result::WRITE_FAIL:
      return "WRITE_FAIL";
    case Result::OTADATA_FAIL:
      return "OTADATA_FAIL";
  }
  return "?";
}

uint16_t runningPartitionChipId() {
  // esp_partition_read hits SPI flash; cache the running slot's chip_id so we
  // only pay that cost once per boot. The running image is immutable at
  // runtime, so a function-local static is safe here.
  static uint16_t cached = [] {
    const esp_partition_t* run = esp_ota_get_running_partition();
    if (!run) return static_cast<uint16_t>(0xFFFF);
    uint16_t id = 0xFFFF;
    // chip_id sits at offset 12 of esp_image_header_t. memcpy target is a
    // uint16_t local, so RISC-V alignment is guaranteed.
    if (esp_partition_read(run, 12, &id, sizeof(id)) != ESP_OK) return static_cast<uint16_t>(0xFFFF);
    return id;
  }();
  return cached;
}

namespace {
struct Context {
  explicit Context(const char* source) : path(source) {}
  const char* path;
  HalFile file;
  std::unique_ptr<uint8_t[]> scratch;
  mbedtls_sha256_context sha;
  const esp_partition_t* partition = nullptr;
  ProgressCb progress = nullptr;
  void* progressContext = nullptr;
};
size_t operation(void* opaque, uint32_t op, size_t a, size_t b, uint8_t* bytes) {
  auto& c = *static_cast<Context*>(opaque);
  switch (op) {
    case XP_FLASH_OPEN:
      return Storage.openFileForRead("FLASH", c.path, c.file) && c.file;
    case XP_FLASH_CLOSE:
      if (c.file.isOpen()) c.file.close();
      return 0;
    case XP_FLASH_SIZE:
      return c.file.fileSize();
    case XP_FLASH_CHIP:
      return runningPartitionChipId();
    case XP_FLASH_ALLOCATE:
      c.scratch = makeUniqueNoThrow<uint8_t[]>(4096);
      return reinterpret_cast<size_t>(c.scratch.get());
    case XP_FLASH_FREE:
      c.scratch.reset();
      return 0;
    case XP_FLASH_READ:
      return static_cast<size_t>(c.file.read(bytes, a));
    case XP_FLASH_SHA_INIT:
      mbedtls_sha256_init(&c.sha);
      mbedtls_sha256_starts(&c.sha, 0);
      return 0;
    case XP_FLASH_SHA_UPDATE:
      mbedtls_sha256_update(&c.sha, bytes, a);
      return 0;
    case XP_FLASH_SHA_FINISH:
      mbedtls_sha256_finish(&c.sha, bytes);
      return 0;
    case XP_FLASH_SHA_FREE:
      mbedtls_sha256_free(&c.sha);
      return 0;
    case XP_FLASH_PARTITION:
      c.partition = esp_ota_get_next_update_partition(nullptr);
      return c.partition != nullptr;
    case XP_FLASH_PARTITION_SIZE:
      return c.partition->size;
    case XP_FLASH_ERASE:
      return esp_partition_erase_range(c.partition, a, b) == ESP_OK;
    case XP_FLASH_WRITE:
      return esp_partition_write(c.partition, a, bytes, b) == ESP_OK;
    case XP_FLASH_SWITCH:
      return ota_boot::switchTo(c.partition);
    case XP_FLASH_PROGRESS:
      if (c.progress) c.progress(a, b, c.progressContext);
      return 0;
    case XP_FLASH_DELAY:
      delay(1);
      return 0;
  }
  return 0;
}
// Events follow the validation/transfer failure sites; a..d are their original scalar arguments.
void diagnostic(void* opaque, uint32_t event, size_t a, size_t b, size_t c, size_t d, const uint8_t* text) {
  const auto& job = *static_cast<Context*>(opaque);
  switch (event) {
    case 0:
      LOG_ERR("FLASH", "validate: open failed: %s", job.path);
      break;
    case 1:
      LOG_ERR("FLASH", "validate: too small: %u", (unsigned)a);
      break;
    case 2:
      LOG_ERR("FLASH", "validate: too large: %u > %u", (unsigned)a, (unsigned)b);
      break;
    case 3:
      LOG_ERR("FLASH", "validate: header read failed");
      break;
    case 4:
      LOG_ERR("FLASH", "validate: bad magic 0x%02X", (unsigned)a);
      break;
    case 5:
      LOG_ERR("FLASH", "validate: wrong chip: image=0x%04X device=0x%04X", (unsigned)a, (unsigned)b);
      break;
    case 6:
      LOG_ERR("FLASH", "validate: seg %u header overruns EOF at %u", (unsigned)a, (unsigned)b);
      break;
    case 7:
      LOG_ERR("FLASH", "validate: seg %u data overruns EOF (%u + %u > %u)", (unsigned)a, (unsigned)b, (unsigned)c,
              (unsigned)d);
      break;
    case 8:
      LOG_ERR("FLASH", "validate: wrong board: image=%s device=%.*s", reinterpret_cast<const char*>(text),
              (int)board_tag::boardNameLen(), board_tag::boardName());
      break;
    case 9:
      LOG_ERR("FLASH", "validate: size mismatch body+pad=%u sha=%u expected=%u actual=%u", (unsigned)a, (unsigned)b,
              (unsigned)c, (unsigned)d);
      break;
    case 10:
      LOG_ERR("FLASH", "validate: checksum mismatch computed=0x%02X stored=0x%02X", (unsigned)a, (unsigned)b);
      break;
    case 11:
      LOG_ERR("FLASH", "validate: SHA256 mismatch");
      break;
    case 12:
      LOG_ERR("FLASH", "no next-update partition");
      break;
    case 13:
      LOG_ERR("FLASH", "image validation failed: %s", resultName(static_cast<Result>(a)));
      break;
    case 14:
      LOG_ERR("FLASH", "open failed: %s", job.path);
      break;
    case 15:
      LOG_INF("FLASH", "src=%s size=%u dest=%s @0x%x partsize=%u", job.path, (unsigned)a, job.partition->label,
              (unsigned)job.partition->address, (unsigned)job.partition->size);
      break;
    case 16:
      LOG_ERR("FLASH", "OOM");
      break;
    case 17:
      LOG_ERR("FLASH", "erase @%u (len=%u) failed", (unsigned)a, (unsigned)b);
      break;
    case 18:
      LOG_ERR("FLASH", "read @%u: got=%d want=%u", (unsigned)a, (int)b, (unsigned)c);
      break;
    case 19:
      LOG_ERR("FLASH", "write @%u failed", (unsigned)a);
      break;
    case 20:
      LOG_ERR("FLASH", "otadata switch failed");
      break;
  }
}
XpFlashServices services(Context& ctx) {
  return {&ctx, operation, diagnostic, reinterpret_cast<const uint8_t*>(board_tag::TAG),
          20 + board_tag::boardNameLen() + 1};
}
}  // namespace
Result validateImageFile(const char* path, size_t partitionSize) {
  Context ctx{path};
  const auto api = services(ctx);
  return static_cast<Result>(xp_flash_validate(&api, partitionSize));
}
Result flashFromSdPath(const char* path, ProgressCb progress, void* opaque, bool validated) {
  Context ctx{path};
  ctx.progress = progress;
  ctx.progressContext = opaque;
  const auto api = services(ctx);
  return static_cast<Result>(xp_flash_run(&api, validated));
}
}  // namespace firmware_flash
