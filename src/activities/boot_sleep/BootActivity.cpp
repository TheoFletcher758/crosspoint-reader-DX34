#include "BootActivity.h"

#include <HalDisplay.h>
#include <GfxRenderer.h>

#include "fontIds.h"
#include "images/BootImage.h"

namespace {

constexpr int kBootImageVerticalOffset = -24;

}

void BootActivity::onEnter() {
  Activity::onEnter();

  renderEmbeddedBootScreen();
}

void BootActivity::renderEmbeddedBootScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int x = (pageWidth - kBootImageWidth) / 2;
  const int centeredY = (pageHeight - kBootImageHeight) / 2;
  const int y = centeredY + kBootImageVerticalOffset;

  renderer.clearScreen();
  renderer.drawImage(BootImage, x, y, kBootImageWidth, kBootImageHeight);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
