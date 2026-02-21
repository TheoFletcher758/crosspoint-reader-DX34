#include "ClearCacheActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ClearCacheActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  state = WARNING;
  requestUpdate();
}

void ClearCacheActivity::onExit() { ActivityWithSubactivity::onExit(); }

void ClearCacheActivity::render(Activity::RenderLock&&) {
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_CLEAR_READING_CACHE), true, EpdFontFamily::REGULAR);

  if (state == WARNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 60, "Clears generated cache files", true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 30, "for EPUB/XTC/TXT books.", true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, "Reading progress is preserved.", true,
                              EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30, "Books may re-index when opened.", true);

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CLEAR_BUTTON), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CLEARING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_CLEARING_CACHE), true, EpdFontFamily::REGULAR);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_CACHE_CLEARED), true, EpdFontFamily::REGULAR);
    std::string resultText = std::to_string(clearedCount) + " " + std::string(tr(STR_ITEMS_REMOVED));
    if (failedCount > 0) {
      resultText += ", " + std::to_string(failedCount) + " " + std::string(tr(STR_FAILED_LOWER));
    }
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, resultText.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_CLEAR_CACHE_FAILED), true,
                              EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_CHECK_SERIAL_OUTPUT));

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void ClearCacheActivity::clearCache() {
  LOG_DBG("CLEAR_CACHE", "Clearing cache...");

  // Open .crosspoint directory
  auto root = Storage.open("/.crosspoint");
  if (!root || !root.isDirectory()) {
    LOG_DBG("CLEAR_CACHE", "Failed to open cache directory");
    if (root) root.close();
    state = FAILED;
    requestUpdate();
    return;
  }

  clearedCount = 0;
  failedCount = 0;
  char name[128];
  std::vector<std::string> cacheDirs;

  // Pass 1: collect cache directories first, then close root before mutating FS.
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    const std::string itemName(name);
    const bool isCacheDir = file.isDirectory() &&
                            (itemName.rfind("epub_", 0) == 0 || itemName.rfind("xtc_", 0) == 0 ||
                             itemName.rfind("txt_", 0) == 0);
    file.close();
    if (isCacheDir) {
      cacheDirs.push_back("/.crosspoint/" + itemName);
    }
  }
  root.close();

  // Pass 2: process each cache directory independently.
  for (const auto& fullPath : cacheDirs) {
    LOG_DBG("CLEAR_CACHE", "Removing cache: %s", fullPath.c_str());

    std::vector<uint8_t> progressData;
    bool hasProgress = false;
    {
      FsFile progressFile;
      const std::string progressPath = fullPath + "/progress.bin";
      if (Storage.openFileForRead("CLEAR_CACHE", progressPath, progressFile)) {
        const auto progressSize = static_cast<size_t>(progressFile.size());
        if (progressSize > 0 && progressSize <= 16) {
          progressData.resize(progressSize);
          const int read = progressFile.read(progressData.data(), progressSize);
          if (read == static_cast<int>(progressSize)) {
            hasProgress = true;
          } else {
            progressData.clear();
          }
        }
        progressFile.close();
      }
    }

    if (!Storage.removeDir(fullPath.c_str())) {
      LOG_ERR("CLEAR_CACHE", "Failed to remove: %s", fullPath.c_str());
      failedCount++;
      continue;
    }

    if (!Storage.mkdir(fullPath.c_str())) {
      LOG_ERR("CLEAR_CACHE", "Failed to recreate cache dir: %s", fullPath.c_str());
      failedCount++;
      continue;
    }

    if (hasProgress) {
      FsFile progressOut;
      const std::string progressPath = fullPath + "/progress.bin";
      if (!Storage.openFileForWrite("CLEAR_CACHE", progressPath, progressOut) ||
          progressOut.write(progressData.data(), progressData.size()) != progressData.size()) {
        LOG_ERR("CLEAR_CACHE", "Failed to restore progress: %s", progressPath.c_str());
        failedCount++;
        if (progressOut) {
          progressOut.close();
        }
        continue;
      }
      progressOut.close();
    }

    clearedCount++;
  }

  LOG_DBG("CLEAR_CACHE", "Cache cleared: %d removed, %d failed", clearedCount, failedCount);

  state = SUCCESS;
  requestUpdate();
}

void ClearCacheActivity::loop() {
  if (state == WARNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      LOG_DBG("CLEAR_CACHE", "User confirmed, starting cache clear");
      {
        RenderLock lock(*this);
        state = CLEARING;
      }
      requestUpdateAndWait();

      clearCache();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      LOG_DBG("CLEAR_CACHE", "User cancelled");
      goBack();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }
}
