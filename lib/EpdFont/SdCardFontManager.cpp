#include "SdCardFontManager.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <SdCardFontRegistry.h>

struct SdCardFontManager::Call {
  SdCardFontManager* owner;
  GfxRenderer* renderer;
};
XpFontManagerServices SdCardFontManager::services(Call* call) {
  static_assert(sizeof(LoadedFont) == sizeof(XpFontLoaded));
  static_assert(offsetof(LoadedFont, fontId) == offsetof(XpFontLoaded, id));
  static_assert(offsetof(LoadedFont, size) == offsetof(XpFontLoaded, size));
  return {
      call,
      [](void* ctx) -> XpFontManagerView {
        const auto& self = *static_cast<Call*>(ctx)->owner;
        return {self.loadedFamilyName_.c_str(), self.loadedFamilyName_.size(),
                reinterpret_cast<const XpFontLoaded*>(self.loaded_.data()), self.loaded_.size()};
      },
      // Rust transfers each successful allocation into loaded_; destroy uses delete.
      []() -> void* { return new (std::nothrow) SdCardFont(); },
      [](void* font, const char* path) { return static_cast<SdCardFont*>(font)->load(path); },
      [](void* font) { delete static_cast<SdCardFont*>(font); },
      [](void* font, uint8_t* styles) {
        const auto& f = *static_cast<SdCardFont*>(font);
        *styles = f.styleCount();
        return f.contentHash();
      },
      [](void* ctx, int32_t id) { return static_cast<Call*>(ctx)->renderer->getFontMap().count(id) != 0; },
      [](void* ctx, int32_t id, void* font) {
        static_cast<Call*>(ctx)->renderer->registerSdCardFont(id, static_cast<SdCardFont*>(font));
      },
      [](void* ctx, XpFontLoaded entry) {
        static_cast<Call*>(ctx)->owner->loaded_.push_back({static_cast<SdCardFont*>(entry.font), entry.id, entry.size});
      },
      [](void* ctx, int32_t id, void* raw) {
        auto* font = static_cast<SdCardFont*>(raw);
        EpdFontFamily family(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
        static_cast<Call*>(ctx)->renderer->insertFont(id, family);
      },
      [](void* ctx, int32_t id) { static_cast<Call*>(ctx)->renderer->removeFont(id); },
      [](void* ctx, size_t index) {
        auto& entries = static_cast<Call*>(ctx)->owner->loaded_;
        entries.erase(entries.begin() + index);
      },
      [](void* ctx) { static_cast<Call*>(ctx)->renderer->clearFallbackFonts(); },
      [](void* ctx) { static_cast<Call*>(ctx)->renderer->clearSdCardFonts(); },
      [](void* ctx) {
        auto& self = *static_cast<Call*>(ctx)->owner;
        self.loaded_.clear();
        self.loadedFamilyName_.clear();
        self.loadedPointSize_ = 0;
      },
      [](void* ctx, const char* family, size_t len, uint8_t size) {
        auto& self = *static_cast<Call*>(ctx)->owner;
        self.loadedFamilyName_.assign(family, len);
        self.loadedPointSize_ = size;
      },
      [](void*, uint32_t event, const char* text, int32_t id, uint32_t size, uint32_t styles) {
        switch (static_cast<XpFontManagerEvent>(event)) {
          case XpFontManagerEvent::Path:
            LOG_ERR("SDMGR", "Font path too long in family %s", text);
            break;
          case XpFontManagerEvent::Allocation:
            LOG_ERR("SDMGR", "Failed to allocate SdCardFont for %s", text);
            break;
          case XpFontManagerEvent::Load:
            LOG_ERR("SDMGR", "Failed to load %s", text);
            break;
          case XpFontManagerEvent::Collision:
            LOG_ERR("SDMGR", "Font ID %d collides with existing font, skipping %s", id, text);
            break;
          case XpFontManagerEvent::Loaded:
            LOG_DBG("SDMGR", "Loaded %s size=%u id=%d styles=%u", text, size, id, styles);
            break;
          case XpFontManagerEvent::EmptyFamily:
            LOG_ERR("SDMGR", "Family %s has no files to load", text);
            break;
        }
      }};
}
namespace {
XpFontFamilyFiles view(const SdCardFontFamilyInfo& family) {
  return {family.name.c_str(), family.name.size(),
          family.root,         reinterpret_cast<const XpFontFile*>(family.files.data()),
          family.files.size(), family.loose};
}
}  // namespace
SdCardFontManager::~SdCardFontManager() {
  Call call{this, nullptr};
  const auto io = services(&call);
  xp_font_manager_destroy(&io);
}
bool SdCardFontManager::loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t size) {
  Call call{this, &renderer};
  const auto io = services(&call);
  const auto files = view(family);
  return xp_font_manager_load(&io, &files, size, 0, static_cast<uint8_t>(XpFontLoadMode::Replace)) != 0;
}
int SdCardFontManager::loadFamilyExtraSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t size,
                                           int id) {
  Call call{this, &renderer};
  const auto io = services(&call);
  const auto files = view(family);
  return xp_font_manager_load(&io, &files, size, id, static_cast<uint8_t>(XpFontLoadMode::ExtraSize));
}
bool SdCardFontManager::loadFamilyVariant(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t size,
                                          int id) {
  Call call{this, &renderer};
  const auto io = services(&call);
  const auto files = view(family);
  return xp_font_manager_load(&io, &files, size, id, static_cast<uint8_t>(XpFontLoadMode::Variant)) != 0;
}
void SdCardFontManager::unloadFont(GfxRenderer& renderer, int id) {
  Call call{this, &renderer};
  const auto io = services(&call);
  xp_font_manager_unload(&io, id, false);
}
void SdCardFontManager::unloadAll(GfxRenderer& renderer) {
  Call call{this, &renderer};
  const auto io = services(&call);
  xp_font_manager_unload(&io, 0, true);
}
int SdCardFontManager::getFontId(const std::string& family, uint8_t size) const {
  Call call{const_cast<SdCardFontManager*>(this), nullptr};
  const auto io = services(&call);
  return xp_font_manager_get(&io, family.c_str(), family.size(), size);
}
