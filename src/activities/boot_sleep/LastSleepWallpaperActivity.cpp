#include "LastSleepWallpaperActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include "CrossPointState.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/FavoriteBmp.h"

namespace {
constexpr int kOptionCount = 4;
}

void LastSleepWallpaperActivity::onEnter() {
  Activity::onEnter();
  selectedOptionIndex = 0;
  if (!hasValidWallpaperPath()) {
    showMessagePopup("No last sleep wallpaper");
  } else {
    requestUpdate();
  }
}

void LastSleepWallpaperActivity::closeActivity() const {
  if (onClose) {
    onClose();
  }
}

void LastSleepWallpaperActivity::showMessagePopup(const std::string& message) {
  messagePopupText = message;
  messagePopupOpen = !message.empty();
  requestUpdate();
}

bool LastSleepWallpaperActivity::dismissMessagePopupOnAnyPress() {
  const bool anyPress =
      mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left) ||
      mappedInput.wasPressed(MappedInputManager::Button::Right) ||
      mappedInput.wasPressed(MappedInputManager::Button::Power);
  if (!anyPress) {
    return false;
  }

  messagePopupOpen = false;
  messagePopupText.clear();
  closeActivity();
  return true;
}

bool LastSleepWallpaperActivity::hasValidWallpaperPath(
    std::string* pathOut) const {
  const std::string path = APP_STATE.lastSleepWallpaperPath;
  if (path.empty() || !Storage.exists(path.c_str())) {
    return false;
  }
  if (pathOut != nullptr) {
    *pathOut = path;
  }
  return true;
}

void LastSleepWallpaperActivity::handleConfirm() {
  std::string lastPath;
  if (!hasValidWallpaperPath(&lastPath)) {
    showMessagePopup("No last sleep wallpaper");
    return;
  }

  if (selectedOptionIndex == 0) {
    closeActivity();
    return;
  }

  if (selectedOptionIndex == 1) {
    if (lastPath.rfind("/sleep pause/", 0) == 0) {
      showMessagePopup("Already in sleep pause");
      return;
    }

    const std::string destDir = "/sleep pause";
    Storage.mkdir(destDir.c_str());
    const auto slashPos = lastPath.find_last_of('/');
    const std::string filename =
        (slashPos == std::string::npos) ? lastPath : lastPath.substr(slashPos + 1);
    const std::string dstPath = destDir + "/" + filename;
    FsFile src, dst;
    bool ok = false;
    if (Storage.openFileForRead("LSW", lastPath.c_str(), src) &&
        Storage.openFileForWrite("LSW", dstPath.c_str(), dst)) {
      uint8_t buf[512];
      ok = true;
      while (src.available()) {
        const int n = src.read(buf, sizeof(buf));
        if (n <= 0 || dst.write(buf, n) != n) {
          ok = false;
          break;
        }
      }
      src.close();
      dst.close();
      if (ok) {
        Storage.remove(lastPath.c_str());
        FavoriteBmp::replacePathReferences(lastPath, dstPath);
        APP_STATE.saveToFile();
      } else {
        Storage.remove(dstPath.c_str());
      }
    } else {
      if (src) {
        src.close();
      }
      if (dst) {
        dst.close();
      }
    }

    if (!ok) {
      showMessagePopup("Move failed");
      return;
    }

    closeActivity();
    return;
  }

  if (selectedOptionIndex == 2) {
    std::string updatedPath;
    const bool makeFavorite = !FavoriteBmp::isFavoritePath(lastPath);
    const auto result =
        FavoriteBmp::setFavorite(lastPath, makeFavorite, &updatedPath);
    if (result == FavoriteBmp::SetFavoriteResult::LimitReached) {
      showMessagePopup(FavoriteBmp::limitReachedPopupMessage());
      return;
    }
    if (result == FavoriteBmp::SetFavoriteResult::RenameConflict) {
      showMessagePopup("Favorite name already exists");
      return;
    }
    if (result != FavoriteBmp::SetFavoriteResult::Success) {
      showMessagePopup("Favorite failed");
      return;
    }

    closeActivity();
    return;
  }

  if (!Storage.remove(lastPath.c_str())) {
    showMessagePopup("Delete failed");
    return;
  }
  FavoriteBmp::removePathReferences(lastPath);
  APP_STATE.saveToFile();
  closeActivity();
}

void LastSleepWallpaperActivity::loop() {
  if (messagePopupOpen) {
    dismissMessagePopupOnAnyPress();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    closeActivity();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedOptionIndex = (selectedOptionIndex + 1) % kOptionCount;
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedOptionIndex =
        (selectedOptionIndex + kOptionCount - 1) % kOptionCount;
    requestUpdate();
  });

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleConfirm();
  }
}

void LastSleepWallpaperActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                      labels.btn4);

  if (messagePopupOpen) {
    GUI.drawPopup(renderer, messagePopupText.c_str());
    renderer.displayBuffer();
    return;
  }

  const std::string favoriteLabel =
      FavoriteBmp::isFavoritePath(APP_STATE.lastSleepWallpaperPath)
          ? "Unfavorite"
          : "Favorite";
  const char* const options[] = {tr(STR_CANCEL), tr(STR_MOVE_TO_SLEEP_PAUSE),
                                 favoriteLabel.c_str(), "Delete"};
  const int rowH = 28;
  const int popupW = pageWidth - 48;
  const int popupH = 44 + kOptionCount * rowH;
  const int popupX = (pageWidth - popupW) / 2;
  const int popupY = (pageHeight - popupH) / 2;

  renderer.fillRect(popupX - 2, popupY - 2, popupW + 4, popupH + 4, true);
  renderer.fillRect(popupX, popupY, popupW, popupH, false);

  const char* title = tr(STR_LAST_SLEEP_WALLPAPER);
  const std::string truncatedTitle =
      renderer.truncatedText(UI_10_FONT_ID, title, popupW - 24);
  const int titleW = renderer.getTextWidth(UI_10_FONT_ID, truncatedTitle.c_str());
  renderer.drawText(UI_10_FONT_ID, popupX + (popupW - titleW) / 2, popupY + 10,
                    truncatedTitle.c_str(), true);

  for (int i = 0; i < kOptionCount; i++) {
    const int rowY = popupY + 40 + i * rowH;
    const bool selected = (i == selectedOptionIndex);
    if (selected) {
      renderer.fillRect(popupX + 6, rowY - 1, popupW - 12, rowH - 2, true);
    }
    renderer.drawText(UI_10_FONT_ID, popupX + 12, rowY, options[i], !selected);
  }

  renderer.displayBuffer();
}
