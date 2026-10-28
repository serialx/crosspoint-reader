#include "SdCardFontRegistry.h"

#include <xp_ffi.h>

#if CROSSPOINT_VECTOR_FONTS
#include <FtFont.h>
#endif
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <strings.h>  // strcasecmp

#include <algorithm>
#include <cstdlib>
#include <cstring>

// One file entry per installed size is resident from boot; keep it to the
// name pointer plus two bytes.
static_assert(sizeof(SdCardFontFileInfo) == sizeof(XpFontFile));
static_assert(offsetof(SdCardFontFileInfo, pointSize) == offsetof(XpFontFile, size));
static_assert(offsetof(SdCardFontFileInfo, style) == offsetof(XpFontFile, style));

// --- SdCardFontFamilyInfo helpers ---

const SdCardFontFileInfo* SdCardFontFamilyInfo::findFile(uint8_t size, uint8_t style) const {
  const auto index =
      xp_font_file_nearest(reinterpret_cast<const XpFontFile*>(files.data()), files.size(), size, style, true);
  return index < 0 ? nullptr : &files[index];
}
const SdCardFontFileInfo* SdCardFontFamilyInfo::findNearestSize(uint8_t size, uint8_t style) const {
  const auto index =
      xp_font_file_nearest(reinterpret_cast<const XpFontFile*>(files.data()), files.size(), size, style, false);
  return index < 0 ? nullptr : &files[index];
}

bool SdCardFontFamilyInfo::hasSize(uint8_t size) const {
  return xp_font_has_size(reinterpret_cast<const XpFontFile*>(files.data()), files.size(), size);
}

std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const {
  std::vector<uint8_t> sizes;
  xp_font_sizes(reinterpret_cast<const XpFontFile*>(files.data()), files.size(), &sizes,
                [](void* ctx, uint8_t size) { static_cast<std::vector<uint8_t>*>(ctx)->push_back(size); });
  return sizes;
}

namespace {
// Heap copy of a filename for SdCardFontFileInfo::name; null when out of memory.
std::unique_ptr<char[]> copyFileName(const char* filename) {
  const size_t bytes = strlen(filename) + 1;
  auto copy = makeUniqueNoThrow<char[]>(bytes);
  if (!copy) {
    LOG_ERR("SDREG", "OOM: %zu byte filename %s", bytes, filename);
    return nullptr;
  }
  memcpy(copy.get(), filename, bytes);
  return copy;
}

// Loaders rebuild the path through filePath() into a PATH_BUFFER_SIZE buffer,
// so a file whose path does not fit can never be opened: drop it at discovery.
// `family` must already carry its name, root and loose flag.
bool pathFits(const SdCardFontFamilyInfo& family, const SdCardFontFileInfo& file) {
  char path[SdCardFontFamilyInfo::PATH_BUFFER_SIZE];
  return family.filePath(file, path, sizeof(path));
}
}  // namespace

bool SdCardFontFamilyInfo::filePath(const SdCardFontFileInfo& file, char* out, size_t outSize) const {
  return xp_font_file_path(root, name.c_str(), file.name.get(), file.pointSize, loose, reinterpret_cast<uint8_t*>(out),
                           outSize);
}

bool SdCardFontRegistry::parseFilename(const char* filename, uint8_t& size, uint8_t& style) {
  if (!xp_sdfont_filename(reinterpret_cast<const uint8_t*>(filename), strlen(filename), &size)) return false;
  style = 0;
  return true;
}
bool SdCardFontRegistry::isCanonicalCpfontName(const char* filename, const std::string& family, uint8_t size) {
  return xp_font_canonical(filename, reinterpret_cast<const uint8_t*>(family.data()), family.size(), size);
}

// --- SdCardFontRegistry ---

#if CROSSPOINT_VECTOR_FONTS

// FtFont::ReadFn over a HalFile (absolute-offset reads; count 0 is a seek probe).
unsigned long SdCardFontRegistry::halFileRead(void* ctx, const unsigned long offset, unsigned char* buffer,
                                              const unsigned long count) {
  auto* f = static_cast<HalFile*>(ctx);
  if (f == nullptr || !*f) return 0;
  if (!f->seek(static_cast<size_t>(offset))) return 0;
  if (count == 0) return 0;
  const int n = f->read(buffer, count);
  return n < 0 ? 0 : static_cast<unsigned long>(n);
}

void SdCardFontRegistry::refineVectorStyles(const char* dirPath, std::vector<SdCardFontFileInfo>& files) {
  using freeink::font::FtFont;
  // Read each face's real weight + italic flag (inspectStream reads only the
  // sfnt header tables, no face is retained), then pick the four roles
  // DETERMINISTICALLY by design weight: the upright face nearest 400 is
  // regular, nearest 700 is bold; same for the italics. This is independent of
  // SD directory order — a Regular/Medium/Semibold/Bold/Black family always
  // resolves to Regular + Bold, not to whichever file happened to enumerate
  // first. An unreadable face falls back to its filename-derived role
  // (Regular/Bold tokens → 400/700).
  using Candidate = XpFontCandidate;
  std::vector<Candidate> cands;
  cands.reserve(files.size());
  char path[SdCardFontFamilyInfo::PATH_BUFFER_SIZE];
  for (size_t i = 0; i < files.size(); ++i) {
    Candidate c{i, static_cast<uint16_t>((files[i].style & 1) ? 700 : 400), (files[i].style & 2) != 0};
    snprintf(path, sizeof(path), "%s/%s", dirPath, files[i].name.get());
    HalFile f = Storage.open(path);
    if (f && !f.isDirectory()) {
      FtFont::FaceInfo face;
      if (FtFont::inspectStream(&halFileRead, &f, static_cast<unsigned long>(f.size()), face) ==
          FtFont::InspectResult::Ok) {
        c.weight = face.weight;
        c.italic = face.italic;
      }
    }
    cands.push_back(c);
  }

  int32_t indices[4];
  const bool promoted =
      xp_font_select_faces(cands.data(), cands.size(), reinterpret_cast<const XpFontFile*>(files.data()), indices);
  if (indices[0] < 0) return;
  if (promoted) LOG_DBG("SDREG", "No upright face in %s — promoting %s", dirPath, files[indices[0]].name.get());
  std::vector<SdCardFontFileInfo> selected;
  selected.reserve(4);
  for (uint8_t role = 0; role < 4; ++role) {
    if (indices[role] < 0) continue;
    SdCardFontFileInfo info = std::move(files[indices[role]]);
    info.style = role;
    selected.push_back(std::move(info));
  }
  if (selected.size() < files.size()) {
    LOG_DBG("SDREG", "%s: %u of %u faces selected by weight", dirPath, static_cast<unsigned>(selected.size()),
            static_cast<unsigned>(files.size()));
  }
  files = std::move(selected);
}

#endif  // CROSSPOINT_VECTOR_FONTS

void SdCardFontRegistry::scanRoot(const char* rootPath, std::vector<SdCardFontFamilyInfo>& out) {
  struct ScanIo {
    std::vector<SdCardFontFamilyInfo>& out;
    HalFile root, dir;
    SdCardFontFamilyInfo family;
    std::vector<SdCardFontFileInfo> cpfont, vector;
  } io{out};
  const XpFontDiscovery services{
      &io,
      CROSSPOINT_VECTOR_FONTS,
      [](void* ctx, const char* path, bool root) -> int32_t {
        auto& io = *static_cast<ScanIo*>(ctx);
        auto& file = root ? io.root : io.dir;
        file = Storage.open(path);
        return !file ? 0 : file.isDirectory() ? 1 : -1;
      },
      [](void* ctx, bool root, uint8_t* name, bool* directory) {
        auto& io = *static_cast<ScanIo*>(ctx);
        HalFile entry = (root ? io.root : io.dir).openNextFile();
        if (!entry) return false;
        *directory = entry.isDirectory();
        if (root || !*directory) entry.getName(reinterpret_cast<char*>(name), 128);
        entry.close();
        return true;
      },
      [](void* ctx, bool root) {
        auto& io = *static_cast<ScanIo*>(ctx);
        (root ? io.root : io.dir) = HalFile{};
      },
      [](void* ctx) { return static_cast<ScanIo*>(ctx)->out.size(); },
      [](void* ctx, size_t i) { return static_cast<ScanIo*>(ctx)->out[i].name.c_str(); },
      [](void* ctx, const char* root, const uint8_t* name, size_t len, bool loose) {
        auto& family = static_cast<ScanIo*>(ctx)->family;
        family.name.assign(reinterpret_cast<const char*>(name), len);
        family.root = root;
        family.loose = loose;
        family.vector = loose;
      },
      [](void* ctx, bool vector, size_t* count) {
        auto& io = *static_cast<ScanIo*>(ctx);
        const auto& files = vector ? io.vector : io.cpfont;
        *count = files.size();
        return reinterpret_cast<const XpFontFile*>(files.data());
      },
      [](void* ctx, bool vector, const char* name, uint8_t size, uint8_t style, bool canonical) {
        auto& io = *static_cast<ScanIo*>(ctx);
        SdCardFontFileInfo file;
        if (!canonical) {
          file.name = copyFileName(name);
          if (!file.name) return false;
        }
        file.pointSize = size;
        file.style = style;
        if (!pathFits(io.family, file)) {
          char dir[SdCardFontFamilyInfo::PATH_BUFFER_SIZE];
          if (io.family.loose)
            snprintf(dir, sizeof(dir), "%s", io.family.root);
          else
            snprintf(dir, sizeof(dir), "%s/%s", io.family.root, io.family.name.c_str());
          if (io.family.loose) {
            LOG_ERR("SDREG", "Path too long for %s in %s — skipping", name, io.family.root);
          } else if (vector) {
            LOG_ERR("SDREG", "Path too long for %s in %s — skipping", name, dir);
          } else {
            LOG_ERR("SDREG", "Path too long for %s in %s — skipping", name, dir);
          }
          return false;
        }
        (vector ? io.vector : io.cpfont).push_back(std::move(file));
        return true;
      },
      [](void* ctx, bool vector, const char* dir) {
        auto& io = *static_cast<ScanIo*>(ctx);
#if CROSSPOINT_VECTOR_FONTS
        if (vector && dir) refineVectorStyles(dir, io.vector);
#endif
        io.family.vector = vector;
        io.family.files = std::move(vector ? io.vector : io.cpfont);
        io.out.push_back(std::move(io.family));
        return io.out.back().files.size();
      },
      [](void*, uint32_t event, const char* a, const char* b, size_t n) {
        switch (static_cast<XpFontDiscoveryEvent>(event)) {
          case XpFontDiscoveryEvent::MissingRoot:
            LOG_DBG("SDREG", "Fonts directory not found: %s", a);
            break;
          case XpFontDiscoveryEvent::NotDirectory:
            LOG_ERR("SDREG", "Fonts path is not a directory: %s", a);
            break;
          case XpFontDiscoveryEvent::Duplicate:
            LOG_ERR("SDREG", "Duplicate font %s in %s — skipping", a, b);
            break;
          case XpFontDiscoveryEvent::Family:
            LOG_DBG("SDREG", "Found family: %s (%d files) in %s", a, static_cast<int>(n), b);
            break;
          case XpFontDiscoveryEvent::Vector:
            LOG_DBG("SDREG", "Found vector font: %s in %s", a, b);
            break;
        }
      },
      [](void* ctx) {
        auto& io = *static_cast<ScanIo*>(ctx);
        io.family = {};
        std::vector<SdCardFontFileInfo>().swap(io.vector);
        std::vector<SdCardFontFileInfo>().swap(io.cpfont);
      }};
  xp_font_scan_root(&services, rootPath);
}

bool SdCardFontRegistry::discover() {
  const XpFontRegistry io{
      this,
      [](void* ctx) { static_cast<SdCardFontRegistry*>(ctx)->families_.clear(); },
      [](void* ctx, const char* root) { scanRoot(root, static_cast<SdCardFontRegistry*>(ctx)->families_); },
      [](void* ctx) {
        auto& families = static_cast<SdCardFontRegistry*>(ctx)->families_;
        std::sort(families.begin(), families.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        return families.size();
      },
      [](void* ctx, size_t n) { static_cast<SdCardFontRegistry*>(ctx)->families_.resize(n); },
      [](void* ctx) {
        auto& families = static_cast<SdCardFontRegistry*>(ctx)->families_;
        shrinkToFit(families);
        LOG_DBG("SDREG", "Discovery complete: %d families", static_cast<int>(families.size()));
        return families.size();
      }};
  return xp_font_discover(&io, FONTS_DIR_HIDDEN, FONTS_DIR_VISIBLE, MAX_SD_FAMILIES);
}

const char* SdCardFontRegistry::findFamilyRoot(const char* familyName) {
  return xp_font_root(
      familyName, FONTS_DIR_HIDDEN, FONTS_DIR_VISIBLE, [](const char* path) { return Storage.exists(path); }, false);
}
const char* SdCardFontRegistry::defaultWriteRoot() {
  return xp_font_root(
      nullptr, FONTS_DIR_HIDDEN, FONTS_DIR_VISIBLE, [](const char* path) { return Storage.exists(path); }, true);
}
const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string& name) const {
  const auto i = getFamilyIndex(name);
  return i < 0 ? nullptr : &families_[i];
}
int SdCardFontRegistry::getFamilyIndex(const std::string& name) const {
  return xp_font_find_family(
      this, families_.size(),
      [](const void* ctx, size_t i) { return static_cast<const SdCardFontRegistry*>(ctx)->families_[i].name.c_str(); },
      reinterpret_cast<const uint8_t*>(name.data()), name.size());
}
