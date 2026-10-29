#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <functional>

#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BookmarkUtil.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;

std::string getBookCachePath(const std::string& path) {
  const char* prefix = nullptr;
  if (FsHelpers::hasReflowableBookExtension(path)) {
    prefix = "epub_";
  } else if (FsHelpers::hasXtcExtension(path)) {
    prefix = "xtc_";
  } else {
    return "";
  }
  return std::string("/.crosspoint/") + prefix + std::to_string(std::hash<std::string>{}(path));
}

bool moveStatePath(const std::string& oldPath, const std::string& newPath, bool& moved) {
  moved = false;
  if (oldPath.empty() || !Storage.exists(oldPath.c_str())) return true;
  if (Storage.exists(newPath.c_str())) {
    LOG_ERR("FileBrowser", "Rename state target already exists: %s", newPath.c_str());
    return false;
  }
  moved = Storage.rename(oldPath.c_str(), newPath.c_str());
  if (!moved) LOG_ERR("FileBrowser", "Failed to move rename state: %s -> %s", oldPath.c_str(), newPath.c_str());
  return moved;
}

void rollBackStatePath(const std::string& oldPath, const std::string& newPath, const bool moved) {
  if (moved && !Storage.rename(newPath.c_str(), oldPath.c_str())) {
    LOG_ERR("FileBrowser", "Failed to roll back rename state: %s -> %s", newPath.c_str(), oldPath.c_str());
  }
}
}  // namespace

std::string getFileExtension(const std::string& filename);

void formatFileName(const std::string& filename, char* buffer, const size_t bufferSize) {
  if (filename.empty()) {
    buffer[0] = '\0';
    return;
  }
  const bool isDirectory = filename.back() == '/';
  const size_t dot = isDirectory ? filename.size() - 1 : filename.rfind('.');
  const int length = static_cast<int>(dot == std::string::npos ? filename.size() : dot);
  const char* format = isDirectory && !UITheme::getInstance().getTheme().showsFileIcons() ? "[%.*s]" : "%.*s";
  snprintf(buffer, bufferSize, format, length, filename.c_str());
  // Compose only the display copy; filesystem lookup needs the raw entry bytes.
  utf8ComposeNfcInPlace(buffer);
}

void formatFileExtension(const std::string& filename, char* buffer, const size_t bufferSize) {
  buffer[0] = '\0';
  if (filename.empty() || filename.back() == '/') return;
  if (const char* extension = strrchr(filename.c_str(), '.')) snprintf(buffer, bufferSize, "%s", extension);
}

FileBrowserActivity::FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::string initialPath, const Mode mode)
    : RustActivityAdapter("FileBrowser", renderer, mappedInput),
      mode(mode),
      basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}

void FileBrowserActivity::loadFiles() {
  files.clear();
  prewarmedStart = -1;  // new folder contents: re-prewarm the visible window

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    return;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    root.close();
    return;
  }

  // Size `files` once before the fill loop: doubling growth on a large folder
  // holds the old and new string blocks live across the final realloc. The
  // count-only pass reads no names, so filtered-out entries just over-reserve
  // by a few slots.
  size_t entryCount = 0;
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) ++entryCount;
  root.rewindDirectory();
  files.reserve(entryCount);

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    const bool isDirectory = file.isDirectory();
    if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }

    if (isDirectory) {
      files.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else {
      std::string_view filename{fileNameBuffer.get()};
      if (mode == Mode::PickFirmware) {
        // Firmware picker: only show .bin files.
        if (FsHelpers::checkFileExtension(filename, ".bin")) {
          files.emplace_back(filename);
        }
      } else if (FsHelpers::hasReflowableBookExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                 FsHelpers::hasImageExtension(filename)) {
        files.emplace_back(filename);
      }
    }
  }
  root.close();
  FsHelpers::sortFileList(files);
}

// Batch-prewarm the CJK fallback glyphs for a bounded window of display names
// starting at the viewport top — one SD pass per list page (the reader TOC's
// refreshTocWindow pattern) instead of one unbounded pass over the whole
// folder. Getter form: no concatenated copy (a bare-new string append aborts
// under heap pressure). The last index covers the bottom path band: basepath
// (possibly a CJK folder name) draws in the same small font, so it must live
// in the same batch or it would evict the rows' glyphs when the heap gate
// disables union merging. (prewarmFallbackText appends the truncation
// ellipsis.)
void FileBrowserActivity::prewarmRowGlyphs(const int start) {
  const int total = static_cast<int>(files.size());
  int clamped = start;
  if (clamped > total - PREWARM_WINDOW) clamped = total - PREWARM_WINDOW;
  if (clamped < 0) clamped = 0;
  if (clamped == prewarmedStart) return;
  prewarmedStart = clamped;
  const int count = total - clamped < PREWARM_WINDOW ? total - clamped : PREWARM_WINDOW;

  struct PrewarmCtx {
    FileBrowserActivity* self;
    int first;
    int count;
  } prewarmCtx{this, clamped, count};
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        auto* c = const_cast<PrewarmCtx*>(static_cast<const PrewarmCtx*>(ctx));
        if (i < static_cast<uint32_t>(c->count)) {
          formatFileName(c->self->files[c->first + i], c->self->rowNameBuf, sizeof(c->self->rowNameBuf));
          return c->self->rowNameBuf;
        }
        return c->self->basepath.c_str();
      },
      &prewarmCtx, static_cast<uint32_t>(count) + 1);
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();
  xp_ui_file_init(&state, mode == Mode::PickFirmware);
  requestUpdate();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    // The first screen build pulls the viewport to it (ListNav follow-on-build).
    loaded(0, static_cast<int>(findEntry(fileName)));
  } else {
    loadFiles();
  }
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
  fileNameBuffer.reset();
}

// To avoid traversing directories twice (once for cache clearing, once for deletion),
// we do both in one pass here, instead of using Storage.removeDir
bool FileBrowserActivity::removeDirFile(const std::string& fullPath) {
  auto file = Storage.open(fullPath.c_str());
  if (!file) {
    LOG_ERR("FileBrowser", "Failed to open for metadata clearing: %s", fullPath.c_str());
    return false;
  }

  if (!file.isDirectory()) {
    file.close();
    return removeBookFile(fullPath);
  }
  file.close();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    return false;
  }

  // Stack of (dirPath, postOrder): postOrder=true means rmdir this path after children are processed.
  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(16);
  stack.push_back({fullPath, false});

  while (!stack.empty()) {
    auto [currentPath, postOrder] = std::move(stack.back());
    stack.pop_back();

    if (postOrder) {
      if (!Storage.rmdir(currentPath.c_str())) {
        LOG_ERR("FileBrowser", "Failed to rmdir: %s", currentPath.c_str());
        return false;
      }
      continue;
    }

    auto dir = Storage.open(currentPath.c_str());
    if (!dir) {
      LOG_ERR("FileBrowser", "Failed to open dir: %s", currentPath.c_str());
      return false;
    }
    if (!dir.isDirectory()) {
      LOG_ERR("FileBrowser", "Not a directory: %s", currentPath.c_str());
      return false;
    }

    // Push this dir for post-order rmdir (after all children are processed).
    stack.push_back({currentPath, true});

    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
      if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) {
        continue;
      }
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') {
        entryPath += "/";
      }
      entryPath += fileNameBuffer.get();

      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        stack.push_back({std::move(entryPath), false});
      } else {
        if (!removeBookFile(entryPath)) {
          LOG_ERR("FileBrowser", "Failed to remove file: %s", entryPath.c_str());
          return false;
        }
      }
    }
  }

  return true;
}

void FileBrowserActivity::activateSelected() {
  XpUiEffect effect{};
  {
    ModelLock lock(*this);
    XpUiInput input{};
    input.released = XP_UI_CONFIRM;
    const auto f = facts(state.list.selected);
    effect = xp_ui_file_update(&state, &input, &f).effect;
  }
  applyRustEffect(effect);
}

void FileBrowserActivity::showEntryActions() {
  if (mode != Mode::Books || files.empty() || optionPopup.isActive() || state.list.selected < 0 ||
      state.list.selected >= listCount()) {
    return;
  }

  const bool isDirectory = files[state.list.selected].back() == '/';
  static constexpr StrId FILE_OPTIONS[] = {StrId::STR_OPEN, StrId::STR_DELETE, StrId::STR_RENAME};
  static constexpr StrId DIRECTORY_OPTIONS[] = {StrId::STR_OPEN, StrId::STR_DELETE};
  optionPopup.show(StrId::STR_FILENAME, isDirectory ? DIRECTORY_OPTIONS : FILE_OPTIONS, isDirectory ? 2 : 3, 0,
                   [this](const int index) {
                     if (index == 0) {
                       activateSelected();
                     } else if (index == 1) {
                       deleteSelected();
                     } else if (index == 2) {
                       startRename();
                     }
                   });
  requestUpdate();
}

void FileBrowserActivity::deleteSelected() {
  if (files.empty() || state.list.selected < 0 || state.list.selected >= listCount()) return;

  std::string cleanBasePath = basepath;
  if (cleanBasePath.back() != '/') cleanBasePath += "/";
  const std::string entry = files[state.list.selected];
  const std::string fullPath = cleanBasePath + entry;

  auto handler = [this, fullPath](const ActivityResult& res) {
    if (res.isCancelled) {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
      return;
    }

    LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
    if (!removeDirFile(fullPath)) {
      LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
      return;
    }

    LOG_DBG("FileBrowser", "Deleted successfully");
    {
      RenderLock lock(*this);
      loadFiles();
      loaded(XP_UI_FILE_DELETE);
    }
    requestUpdate(true);
  };

  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ITEM_CONFIRM),
                                                              utf8ComposeNfc(entry));
  if (!confirmation) {
    LOG_ERR("FileBrowser", "OOM: delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), std::move(handler));
}

void FileBrowserActivity::startRename() {
  if (files.empty() || state.list.selected < 0 || state.list.selected >= listCount()) return;

  const std::string oldEntry = files[state.list.selected];
  if (oldEntry.back() == '/') return;

  std::string cleanBasePath = basepath;
  if (cleanBasePath.back() != '/') cleanBasePath += "/";
  const std::string oldPath = cleanBasePath + oldEntry;
  const std::string extension = getFileExtension(oldEntry);
  const std::string initialStem = utf8ComposeNfc(oldEntry.substr(0, oldEntry.size() - extension.size()));
  const size_t maxStemLength = NAME_BUFFER_SIZE - extension.size() - 1;
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RENAME), initialStem,
                                                           maxStemLength, InputType::Text);
  if (!keyboard) {
    LOG_ERR("FileBrowser", "OOM: rename keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, oldPath, oldEntry, extension](const ActivityResult& result) {
    if (result.isCancelled) return;
    renameSelectedFile(oldPath, oldEntry, std::get<KeyboardResult>(result.data).text, extension);
  });
}

void FileBrowserActivity::renameSelectedFile(const std::string& oldPath, const std::string& oldEntry,
                                             const std::string& newStem, const std::string& extension) {
  const std::string newEntry = newStem + extension;
  if (newStem.empty() || newEntry.size() >= NAME_BUFFER_SIZE || !FsHelpers::isSafePathComponent(newEntry)) {
    LOG_ERR("FileBrowser", "Invalid rename target: %s", newEntry.c_str());
    return;
  }
  if (newEntry == utf8ComposeNfc(oldEntry)) return;

  const std::string parentPath = FsHelpers::extractFolderPath(oldPath);
  const std::string newPath = (parentPath == "/" ? parentPath : parentPath + "/") + newEntry;
  if (Storage.exists(newPath.c_str())) {
    LOG_ERR("FileBrowser", "Rename target already exists: %s", newPath.c_str());
    return;
  }

  const std::string oldCachePath = getBookCachePath(oldPath);
  const std::string newCachePath = getBookCachePath(newPath);
  const bool hasBookmarks = FsHelpers::hasReflowableBookExtension(oldPath);
  const std::string oldBookmarkPath = hasBookmarks ? BookmarkUtil::getBookmarkPath(oldPath) : "";
  const std::string newBookmarkPath = hasBookmarks ? BookmarkUtil::getBookmarkPath(newPath) : "";
  bool cacheMoved = false;
  bool bookmarksMoved = false;
  if (!moveStatePath(oldCachePath, newCachePath, cacheMoved)) return;
  if (!moveStatePath(oldBookmarkPath, newBookmarkPath, bookmarksMoved)) {
    rollBackStatePath(oldCachePath, newCachePath, cacheMoved);
    return;
  }
  if (!ClippingStore::moveBook(oldPath, newPath)) {
    LOG_ERR("FileBrowser", "Failed to rename file: %s -> %s", oldPath.c_str(), newPath.c_str());
    rollBackStatePath(oldBookmarkPath, newBookmarkPath, bookmarksMoved);
    rollBackStatePath(oldCachePath, newCachePath, cacheMoved);
    return;
  }

  RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath);
  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    if (!APP_STATE.saveToFile()) LOG_ERR("FileBrowser", "Failed to save renamed open-book path");
  }

  {
    RenderLock lock(*this);
    loadFiles();
    loaded(XP_UI_FILE_RENAMED, static_cast<int>(findEntry(newEntry)));
  }
  requestUpdate(true);
}

void FileBrowserActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  RustActivityAdapter::render(std::move(lock));
}

std::string getFileExtension(const std::string& filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  const auto entry = std::find(files.begin(), files.end(), name);
  return entry != files.end() ? static_cast<size_t>(entry - files.begin()) : 0;
}

XpUiFileFacts FileBrowserActivity::facts(int selected) const {
  return {static_cast<uint16_t>(files.size()),
          static_cast<uint8_t>(basepath == "/"),
          static_cast<uint8_t>(selected >= 0 && selected < listCount() && files[selected].back() == '/'),
          static_cast<uint8_t>(nativeLongPress),
          static_cast<uint8_t>(nativeLongTouch),
          static_cast<uint8_t>(mappedInput.hasTouch()),
          0};
}

void FileBrowserActivity::loaded(uint8_t reason, int selected) {
  ModelLock lock(*this);
  xp_ui_file_loaded(&state, reason, selected, files.size());
}

void FileBrowserActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  mappedInput.wasSwipe(&nativeTouchX, &nativeTouchY);
  nativeLongPress = mode == Mode::Books && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, GO_HOME_MS);
  bool ready;
  {
    ModelLock lock(*this);
    ready = publication.ready;
  }
  nativeLongTouch = ready && mappedInput.wasScreenLongPress(nativeTouchX, nativeTouchY);
  RustActivityAdapter::loop();
}

XpUiInterest FileBrowserActivity::rustInterest(uint32_t now) const {
  auto interest = xp_ui_file_interest(&state, now);
  if (nativeLongPress || nativeLongTouch) {
    interest.held = 0;
    interest.deadline_enabled = 1;
    interest.deadline_ms = now;
  }
  return interest;
}

XpUiEffect FileBrowserActivity::updateRust(const XpUiInput& source) {
  auto input = source;
  if (publication.ready && (input.touch_flags || nativeLongTouch || input.swipe == XP_UI_SWIPE_LEFT)) {
    XpUiInteraction hit{};
    const bool alternate = nativeLongTouch || input.swipe == XP_UI_SWIPE_LEFT;
    if (xp_ui_home_list_hit(&publishedHits, alternate ? nativeTouchX : input.touch_x,
                            alternate ? nativeTouchY : input.touch_y, &hit)) {
      input.action = hit.action;
      input.value = hit.value;
    } else
      nativeLongTouch = false;
  }
  auto f = facts(state.list.selected);
  f.touch_directory =
      input.action == 1 && input.value >= 0 && input.value < listCount() && files[input.value].back() == '/';
  const auto update = xp_ui_file_update(&state, &input, &f);
  touchFeedback = update.haptic;
  return update.effect;
}

void FileBrowserActivity::applyRustEffect(const XpUiEffect& effect) {
  if (touchFeedback) {
    haptic_feedback::touchAction(nativeLongTouch);
    touchFeedback = 0;
  }
  switch (effect.kind) {
    case XP_UI_SCROLL:
      LOG_DBG("LIST", "%s swipe delta=%d count=%d", name.c_str(), effect.value, listCount());
      break;
    case XP_UI_FILE_ROOT: {
      RenderLock lock(*this);
      basepath = "/";
      loadFiles();
      loaded(XP_UI_FILE_ROOT);
      lock.unlock();
      requestUpdate();
      break;
    }
    case XP_UI_FILE_PARENT: {
      const std::string oldPath = basepath;
      RenderLock lock(*this);
      basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
      if (basepath.empty()) basepath = "/";
      loadFiles();
      const std::string dirName = oldPath.substr(oldPath.find_last_of('/') + 1) + "/";
      loaded(XP_UI_FILE_PARENT, static_cast<int>(findEntry(dirName)));
      lock.unlock();
      requestUpdate();
      break;
    }
    case XP_UI_CANCEL: {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      break;
    }
    case XP_UI_FILE_HOME:
      onGoHome();
      break;
    case XP_UI_FILE_OPTIONS:
      showEntryActions();
      break;
    case XP_UI_FILE_DELETE:
      deleteSelected();
      break;
    case XP_UI_FILE_PICK: {
      std::string clean = basepath;
      if (clean.back() != '/') clean += "/";
      ActivityResult result{FilePathResult{clean + files[effect.value]}};
      result.isCancelled = false;
      setResult(std::move(result));
      finish();
      break;
    }
    case XP_UI_FILE_DIRECTORY:
    case XP_UI_FILE_OPEN: {
      RenderLock lock(*this);
      if (basepath.back() != '/') basepath += "/";
      const std::string entry = files[effect.value];
      if (effect.kind == XP_UI_FILE_DIRECTORY) {
        basepath += entry.substr(0, entry.size() - 1);
        loadFiles();
        loaded(XP_UI_FILE_DIRECTORY);
        lock.unlock();
        requestUpdate();
      } else {
        const std::string fullPath = basepath + entry;
        lock.unlock();
        onSelectBook(fullPath);
      }
      break;
    }
  }
}

const char* FileBrowserActivity::text(uint32_t h) {
  if (h >= XP_UI_FILE_PATH_SUFFIX && h - XP_UI_FILE_PATH_SUFFIX <= basepath.size())
    return basepath.c_str() + h - XP_UI_FILE_PATH_SUFFIX;
  if (h > 0 && h < 0x10000000u) {
    const size_t index = (h - 1) / 4;
    if (index >= files.size()) return nullptr;
    switch ((h - 1) % 4) {
      case 0:
        formatFileName(files[index], rowNameBuf, sizeof(rowNameBuf));
        return rowNameBuf;
      case 1:
        formatFileExtension(files[index], rowExtBuf, sizeof(rowExtBuf));
        return rowExtBuf;
      default:
        return nullptr;
    }
  }
  switch (h) {
    case XP_UI_FILE_PATH:
      if (!frame.next.path_truncated) return basepath.c_str();
      snprintf(rowNameBuf, 256, "\xe2\x80\xa6%s", basepath.c_str() + frame.next.path_start);
      return rowNameBuf;
    case XP_UI_FILE_EMPTY:
      return tr(STR_NO_FILES_FOUND);
    case XP_UI_FILE_NO_BIN:
      return tr(STR_NO_BIN_FILES);
    case XP_UI_FILE_ELLIPSIS:
      return "\xe2\x80\xa6";
    default:
      return nullptr;
  }
}

XpUiHomeRows FileBrowserActivity::rows() {
  return {this,
          [](void* p, uint16_t first, uint16_t) -> uint8_t {
            static_cast<FileBrowserActivity*>(p)->prewarmRowGlyphs(first);
            return true;
          },
          [](void* p, uint16_t index, XpUiHomeRow* out) -> uint8_t {
            auto& self = *static_cast<FileBrowserActivity*>(p);
            if (index >= self.files.size()) return false;
            const uint32_t h = static_cast<uint32_t>(index) * 4 + 1;
            const auto icon = listIconFor(UITheme::getFileIcon(self.files[index]));
            formatFileExtension(self.files[index], self.rowExtBuf, sizeof(self.rowExtBuf));
            *out = {h, 0, self.rowExtBuf[0] ? h + 1 : 0, icon ? h + 3 : 0, 0, static_cast<int16_t>(icon.width), 0};
            return true;
          },
          [](void* p, uint32_t h, int32_t f, uint8_t b) -> int32_t {
            auto& self = *static_cast<FileBrowserActivity*>(p);
            const char* s = self.text(h);
            return s ? self.renderer.getTextWidth(f, s, static_cast<EpdFontFamily::Style>(b)) : -1;
          },
          [](void* p, uint32_t h, int32_t f, uint8_t b, uint8_t lines, int16_t width) -> int32_t {
            auto& self = *static_cast<FileBrowserActivity*>(p);
            const char* s = self.text(h);
            if (!s) return -1;
            fui::GfxRendererTarget target(self.renderer);
            target.setFont(0, f);
            fui::TextStyle style;
            style.bold = b;
            style.maxLines = lines;
            return fui::measureWrappedText(target, s, style, width).height;
          },
          [](void* p, int32_t f) -> int32_t { return static_cast<FileBrowserActivity*>(p)->renderer.getLineHeight(f); },
          reinterpret_cast<const uint8_t*>(basepath.data()),
          static_cast<uint32_t>(basepath.size()),
          nullptr};
}

bool FileBrowserActivity::buildRustScene(XpUiScene& scene) {
  {
    ModelLock lock(*this);
    frame.snapshot = state;
  }
  XpUiFileLayout layout{};
  sampleReaderChromeTheme(renderer, layout.theme);
  layout.theme.help_font = SMALL_FONT_ID;
  const auto content = UITheme::getContentArea(renderer);
  layout.theme.safe = {static_cast<int16_t>(content.x), static_cast<int16_t>(content.y),
                       static_cast<int16_t>(content.width), static_cast<int16_t>(content.height)};
  const auto& metrics = UITheme::getInstance().getMetrics();
  layout.hints_height = metrics.buttonHintsHeight;
  layout.side_padding = metrics.contentSidePadding;
  const auto resources = rows();
  const auto f = facts(frame.snapshot.list.selected);
  if (!frame.commands.ready() && !frame.commands.reserve(32)) return false;
  for (;;) {
    const auto buffer = frame.commands.buffer();
    const auto status =
        xp_ui_file_view(&frame.snapshot, &f, &layout, &resources, &buffer, &frame.next, &frame.hits, &scene);
    if (status == XP_UI_HOME_VIEW_OK) {
      return true;
    }
    if (status != XP_UI_HOME_VIEW_CAPACITY || buffer.command_capacity >= 128 ||
        !frame.commands.reserve(std::min<uint16_t>(128, buffer.command_capacity + 4)))
      return false;
  }
}

void FileBrowserActivity::rustPublished() {
  xp_ui_file_publish(&state, &frame.next, frame.snapshot.list.pending_scroll);
  publishedHits = frame.hits;
}

XpUiPaint FileBrowserActivity::rustResources() {
  return {
      this, nullptr, nullptr,
      [](void* p, uint32_t h, uint32_t generation) -> uint8_t {
        auto& self = *static_cast<FileBrowserActivity*>(p);
        if (generation != 1) return false;
        if ((h >= XP_UI_FILE_HEADER_FOLDER && h <= XP_UI_FILE_FOOTER) || h == XP_UI_READER_DELETE_ICON) return true;
        if (h > 0 && h < 0x10000000u && (h - 1) % 4 == 3) return (h - 1) / 4 < self.files.size();
        return self.text(h) != nullptr;
      },
      [](void* p, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
        auto& self = *static_cast<FileBrowserActivity*>(p);
        const auto& r = command->rect;
        const uint32_t h = command->resource;
        if (h >= XP_UI_FILE_HEADER_FOLDER && h <= XP_UI_FILE_HEADER_PICKER) {
          const std::string title = h == XP_UI_FILE_HEADER_PICKER ? tr(STR_SELECT_FIRMWARE_FILE)
                                    : h == XP_UI_FILE_HEADER_ROOT ? tr(STR_SD_CARD)
                                                                  : self.basepath.substr(self.basepath.rfind('/') + 1);
          GUI.drawHeader(self.renderer, Rect{r.x, r.y, r.w, r.h}, title.c_str());
          return;
        }
        if (h == XP_UI_FILE_FOOTER) {
          const auto& next = self.frame.next;
          const auto labels =
              self.mappedInput.mapLabels(next.back_home ? tr(STR_HOME) : tr(STR_BACK),
                                         next.has_rows ? (next.select_file ? tr(STR_SELECT) : tr(STR_OPEN)) : "",
                                         next.has_rows ? tr(STR_DIR_UP) : "", next.has_rows ? tr(STR_DIR_DOWN) : "");
          GUI.drawButtonHints(self.renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
          return;
        }
        if (command->kind == XP_UI_BITMAP) {
          const auto icon = h == XP_UI_READER_DELETE_ICON ? fui::bitmapFromIcon(icon_trash_2_24)
                                                          : listIconFor(UITheme::getFileIcon(self.files[(h - 1) / 4]));
          const auto previous = self.renderer.getClipRect();
          self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
          fui::GfxRendererTarget target(self.renderer);
          target.bitmap({r.x, r.y, r.w, r.h}, icon, fui::BitmapMode::Contain,
                        fui::Paint::solid(command->paint == 1 ? fui::Color::White : fui::Color::Black));
          self.renderer.setClipRect(previous[0], previous[1], previous[2], previous[3]);
        } else
          self.renderer.drawUiText(*command, clip, self.text(h));
      }};
}
