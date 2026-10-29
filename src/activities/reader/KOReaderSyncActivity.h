#pragma once
#include <Epub.h>

#include <memory>
#include <string>

#include "KOReaderSyncClient.h"
#include "KOReaderUiActivity.h"
#include "ProgressMapper.h"

class KOReaderSyncActivity final : public KOReaderUiActivity {
 public:
  explicit KOReaderSyncActivity(GfxRenderer&, MappedInputManager&, const std::string& epubPath,
                                CrossPointPosition localPosition, SavedProgressPosition localKoPos,
                                std::string localChapterName, bool pluginEventsOnly = false);
  void onEnter() override;
  void onExit() override;

 private:
#ifdef CROSSPOINT_UI_ORACLE
  friend struct UiOracleKOReader;
  bool oracleFreeze = false;
#endif
  std::shared_ptr<Epub> epub;
  Epub* paintingEpub = nullptr;
  std::string epubPath, localChapterName, documentHash;
  CrossPointPosition localPosition{}, remotePosition{};
  SavedProgressPosition localProgress;
  KOReaderProgress remoteProgress{};
  // Alternating fetch/upload scratch belongs to the synchronous service call.
  KOReaderProgress scratch{};
  std::string alternateHash;
  // These strings/buffers are render-owned for the complete scene lifetime.
  std::string remoteChapter;
  char localChapterFallback[32]{}, remoteValue[64]{}, localValue[64]{}, deviceValue[80]{};
  void onWifiSelectionComplete(bool);
  void ensureEpubLoaded();
  void releaseEpub();
  void performSync();
  void performUpload();
  void startUpload();
  void returnToReader();
  XpKoOptions options() const;
  static uint32_t service(void*, uint32_t, uint32_t, uint32_t);
  uint32_t service(XpKoOperation, uint32_t, uint32_t);
  void snapshotResources() override;
  void prepareText() override;
  const char* resourceText(uint32_t) const override;
  void performEffect(const XpUiEffect&) override;
};
