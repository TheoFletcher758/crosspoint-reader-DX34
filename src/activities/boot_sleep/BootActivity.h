#pragma once
#include "../Activity.h"

class Bitmap;

class BootActivity final : public Activity {
 public:
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Boot", renderer, mappedInput) {}
  void onEnter() override;

 private:
  void renderDefaultBootScreen() const;
  void renderBitmapBootScreen(const Bitmap& bitmap) const;
};
