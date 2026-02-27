#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_CROSSPOINT), true, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 15, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
