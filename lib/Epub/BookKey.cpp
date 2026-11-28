#include "BookKey.h"

#include <DeviceSecret.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_random.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <xp_ffi.h>

#include <memory>
#include <optional>

namespace bookkey {
namespace {
// wolfSSL's GCM tables stay off the task stack and live through the transaction.
struct AesGcm {
  std::unique_ptr<Aes> aes = makeUniqueNoThrow<Aes>();
  bool ready = aes && wc_AesInit(aes.get(), nullptr, INVALID_DEVID) == 0;
  ~AesGcm() {
    if (ready) wc_AesFree(aes.get());
  }
};
struct Context {
  const std::string& bookPath;
  std::optional<AesGcm> gcm;
  std::string path, tmp;
};
XpBookKeyServices services(Context& context, bool (*secret)(void*, uint8_t*)) {
  return {&context,
          secret,
          [](void*, const uint8_t* input, size_t size, uint8_t* out) { return wc_Sha256Hash(input, size, out) == 0; },
          [](void*, uint8_t* bytes, size_t size) { esp_fill_random(bytes, size); },
          [](void* opaque, const XpBookKeyCipher* call) {
            auto& ctx = *static_cast<Context*>(opaque);
            auto& gcm = ctx.gcm.emplace();
            if (!gcm.ready || wc_AesGcmSetKey(gcm.aes.get(), call->kek, 32) != 0) return false;
            if (call->encrypt) {
              return wc_AesGcmEncrypt(gcm.aes.get(), call->output, call->input, call->key_len, call->iv, 12, call->tag,
                                      16, call->aad, call->aad_len) == 0;
            }
            return wc_AesGcmDecrypt(gcm.aes.get(), call->output, call->input, call->key_len, call->iv, 12, call->tag,
                                    16, call->aad, call->aad_len) == 0;
          },
          [](void* opaque, uint8_t* bytes, size_t size) -> int32_t {
            HalFile file;
            if (!Storage.openFileForRead("BKEY", static_cast<Context*>(opaque)->bookPath + ".key", file)) return 0;
            return file.read(bytes, size);
          },
          [](void* opaque, const uint8_t* bytes, size_t size) {
            auto& ctx = *static_cast<Context*>(opaque);
            ctx.path = ctx.bookPath + ".key";
            ctx.tmp = ctx.path + ".tmp";
            HalFile file;
            if (!Storage.openFileForWrite("BKEY", ctx.tmp, file) || file.write(bytes, size) != size) {
              LOG_ERR("BKEY", "Write failed: %s", ctx.tmp.c_str());
              return false;
            }
            return true;
          },
          [](void* opaque) {
            const auto& ctx = *static_cast<Context*>(opaque);
            return Storage.replaceFile(ctx.tmp.c_str(), ctx.path.c_str());
          }};
}
}  // namespace

bool write(const std::string& bookPath, const uint8_t* key, size_t keyLen, int64_t expiresAt) {
  Context context{bookPath, {}, {}, {}};
  const auto io =
      services(context, [](void*, uint8_t* out) { return deviceSecret(*reinterpret_cast<uint8_t (*)[32]>(out)); });
  const int32_t result = xp_book_key_write(key, keyLen, expiresAt, &io);
  if (result == 2) LOG_ERR("BKEY", "Wrap failed");
  return result == 0;
}

size_t read(const std::string& bookPath, uint8_t (&key)[MAX_KEY_LEN], int64_t* expiresAt) {
  Context context{bookPath, {}, {}, {}};
  const auto io =
      services(context, [](void*, uint8_t* out) { return deviceSecret(*reinterpret_cast<uint8_t (*)[32]>(out)); });
  const int32_t result = xp_book_key_read(key, expiresAt, &io);
  if (result < 0) {
    LOG_ERR("BKEY", "Key not wrapped by this device or tampered: %s", bookPath.c_str());
    return 0;
  }
  return static_cast<size_t>(result);
}

struct ReaderContext {
  std::string path;
  Context context;
  uint32_t nvsHandle{};
  explicit ReaderContext(XpAppText path)
      : path(reinterpret_cast<const char*>(path.data), path.len), context{this->path, {}, {}, {}} {}
};
static_assert(sizeof(ReaderContext) <= 24 * sizeof(uintptr_t));
static_assert(alignof(ReaderContext) <= alignof(uintptr_t));
void readerPlatform(void* storage, uint8_t operation, XpAppText path, void* out) {
  auto* context = static_cast<ReaderContext*>(storage);
  switch (static_cast<ReaderOperation>(operation)) {
    case ReaderOperation::Begin:
      new (context) ReaderContext(path);
      *static_cast<XpBookKeyServices*>(out) = services(context->context, [](void*, uint8_t*) { return false; });
      break;
    case ReaderOperation::End:
      context->~ReaderContext();
      break;
    case ReaderOperation::BorrowSecret: {
      auto& secret = *static_cast<ReaderSecret*>(out);
      secret.state = borrowDeviceSecret(&context->nvsHandle, &secret.services);
      break;
    }
    case ReaderOperation::ReleaseSecret:
      releaseDeviceSecret();
      break;
  }
}
}  // namespace bookkey
