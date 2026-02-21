#include "ClearCacheActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

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

  // Iterate through all entries in the directory
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    String itemName(name);

    // Only delete reader cache directories.
    if (file.isDirectory() && (itemName.startsWith("epub_") || itemName.startsWith("xtc_") || itemName.startsWith("txt_"))) {
      String fullPath = "/.crosspoint/" + itemName;
      LOG_DBG("CLEAR_CACHE", "Removing cache: %s", fullPath.c_str());

      file.close();  // Close before attempting to delete

      // Preserve progress.bin before deleting cache directory.
      std::vector<uint8_t> progressData;
      bool hasProgress = false;
      {
        FsFile progressFile;
        const String progressPath = fullPath + "/progress.bin";
        if (Storage.openFileForRead("CLEAR_CACHE", progressPath.c_str(), progressFile)) {
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

      if (Storage.removeDir(fullPath.c_str())) {
        if (hasProgress) {
          const String progressPath = fullPath + "/progress.bin";
          if (!Storage.mkdir(fullPath.c_str())) {
            LOG_ERR("CLEAR_CACHE", "Failed to recreate cache dir for progress restore: %s", fullPath.c_str());
            failedCount++;
            continue;
          }
          FsFile progressOut;
          if (!Storage.openFileForWrite("CLEAR_CACHE", progressPath.c_str(), progressOut) ||
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
      } else {
        LOG_ERR("CLEAR_CACHE", "Failed to remove: %s", fullPath.c_str());
        failedCount++;
      }
    } else {
      file.close();
    }
  }
  root.close();

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
