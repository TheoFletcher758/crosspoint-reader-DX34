#include "BootActivity.h"

#include <string>

#include <HalDisplay.h>
#include <GfxRenderer.h>

#include "fontIds.h"
#include "images/BootImage.h"

namespace {

constexpr int kBootImageVerticalOffset = -24;
constexpr int kVersionTextBottom = 30;

std::string bootVersionText() {
  constexpr const char* kPrefix = "CrossPoint-Mod-DX34-";
  std::string version = CROSSPOINT_VERSION;
  if (version.rfind(kPrefix, 0) == 0) {
    version.erase(0, std::char_traits<char>::length(kPrefix));
  }
  return "[MOD] DX34 " + version;
}

}

void BootActivity::onEnter() {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int x = (pageWidth - kBootImageWidth) / 2;
  const int y = (pageHeight - kBootImageHeight) / 2 + kBootImageVerticalOffset;
  renderer.drawImage(BootImage, x, y, kBootImageWidth, kBootImageHeight);
  const std::string versionText = bootVersionText();
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - kVersionTextBottom,
                            versionText.c_str());
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
