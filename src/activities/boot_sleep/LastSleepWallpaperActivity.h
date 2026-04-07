#pragma once

#include <functional>
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class LastSleepWallpaperActivity final : public Activity {
 public:
  explicit LastSleepWallpaperActivity(
      GfxRenderer& renderer, MappedInputManager& mappedInput,
      const std::function<void()>& onClose,
      const std::function<void()>& onBack = nullptr)
      : Activity("LastSleepWallpaper", renderer, mappedInput),
        onClose(onClose), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  const std::function<void()> onClose;
  const std::function<void()> onBack;
  bool messagePopupOpen = false;
  std::string messagePopupText;
  int selectedOptionIndex = 0;

  void closeActivity() const;
  void clearInvalidWallpaperState() const;
  void showMessagePopup(const std::string& message);
  bool dismissMessagePopupOnAnyPress();
  bool hasValidWallpaperPath(std::string* pathOut = nullptr) const;
  void handleConfirm();
};
