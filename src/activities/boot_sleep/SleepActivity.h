#pragma once
#include "../Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Sleep", renderer, mappedInput) {}
  void onEnter() override;
  static bool randomizeSleepImagePlaylist();
  // Called once on boot: moves overflow images beyond the playlist limit to /sleep pause.
  static void trimSleepFolderToLimit();

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, const char* sourceFilename = nullptr) const;
  void renderBlankSleepScreen() const;
};
