#include "RustReaderDocument.h"

#include <HalStorage.h>
#include <esp_rom_md5.h>

#include <cassert>
#include <new>

namespace {
enum class Operation : uint8_t { Init, Drop, Open, Size, Seek, Read, DigestBegin, DigestAdd, DigestFinish };
struct DocumentIo {
  HalFile file;
  md5_context_t digest;
};
static_assert(sizeof(DocumentIo) <= 32 * sizeof(uintptr_t));
static_assert(alignof(DocumentIo) <= alignof(uintptr_t));
}  // namespace

int64_t readerDocumentIo(void* storage, uint8_t operation, const uint8_t* input, uint8_t* output, uint32_t value) {
  auto* io = static_cast<DocumentIo*>(storage);
  switch (static_cast<Operation>(operation)) {
    case Operation::Init:
      new (io) DocumentIo;
      return 0;
    case Operation::Drop:
      io->~DocumentIo();
      return 0;
    case Operation::Open:
      return Storage.openFileForRead("KODoc", reinterpret_cast<const char*>(input), io->file);
    case Operation::Size:
      return io->file.fileSize();
    case Operation::Seek:
      return io->file.seekSet(value);
    case Operation::Read:
      return io->file.read(output, value);
    case Operation::DigestBegin:
      esp_rom_md5_init(&io->digest);
      return 0;
    case Operation::DigestAdd:
      esp_rom_md5_update(&io->digest, input, value);
      return 0;
    case Operation::DigestFinish:
      esp_rom_md5_final(output, &io->digest);
      return 0;
  }
  assert(false && "Invalid reader document operation");
  return 0;
}
