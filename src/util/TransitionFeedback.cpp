#include "TransitionFeedback.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cctype>

#include "fontIds.h"

namespace TransitionFeedback {
namespace {
bool sActive = false;
int sCurrentPercent = 0;
constexpr int kBarHeight = 36;
}

void show(GfxRenderer& renderer, const char* message) {
  sActive = false;
  if (!message || message[0] == '\0') {
    return;
  }

  std::string upper(message);
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return std::toupper(c); });

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int textWidth =
      renderer.getTextWidth(UI_12_FONT_ID, upper.c_str(), EpdFontFamily::REGULAR);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);

  constexpr int paddingX = 20;
  constexpr int paddingY = 12;
  constexpr int border = 2;
  const int boxW = textWidth + paddingX * 2;
  const int boxH = textHeight + paddingY * 2;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;

  renderer.fillRect(boxX - border, boxY - border,
                    boxW + border * 2, boxH + border * 2, true);
  renderer.fillRect(boxX, boxY, boxW, boxH, false);

  const int textX = boxX + (boxW - textWidth) / 2;
  const int textY = boxY + paddingY - 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, upper.c_str(), true,
                    EpdFontFamily::REGULAR);

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  sActive = true;
}

void showProgressBar(GfxRenderer& renderer, int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  // Progress only moves forward; skip if not advancing
  if (sActive && percent <= sCurrentPercent) {
    return;
  }

  sCurrentPercent = percent;

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int barY = screenH - kBarHeight;
  const int fillW = screenW * percent / 100;

  // Clear bar area then draw filled portion
  renderer.fillRect(0, barY, screenW, kBarHeight, false);
  if (fillW > 0) {
    renderer.fillRect(0, barY, fillW, kBarHeight, true);
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  sActive = true;
}

void sweepProgressBar(GfxRenderer& renderer, int steps) {
  if (steps < 2) steps = 2;
  sCurrentPercent = 0;  // Reset so sweep always starts fresh
  for (int i = 1; i <= steps; i++) {
    showProgressBar(renderer, i * 100 / steps);
    if (i < steps) {
      delay(30);
    }
  }
}

void dismiss(GfxRenderer& renderer) {
  if (!sActive) {
    return;
  }
  sActive = false;
  sCurrentPercent = 0;
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

bool isActive() {
  return sActive;
}

}  // namespace TransitionFeedback
