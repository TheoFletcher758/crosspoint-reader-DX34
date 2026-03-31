#include "TransitionFeedback.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cctype>

#include "fontIds.h"

namespace TransitionFeedback {

void show(GfxRenderer& renderer, const char* message) {
  if (!message || message[0] == '\0') {
    return;
  }

  // Convert to uppercase for consistency with StatusPopup style
  std::string upper(message);
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return std::toupper(c); });

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, upper.c_str(), EpdFontFamily::REGULAR);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);

  constexpr int paddingX = 20;
  constexpr int paddingY = 12;
  const int boxW = textWidth + paddingX * 2;
  const int boxH = textHeight + paddingY * 2;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;

  // Draw framed popup centered on screen
  renderer.fillRect(boxX - 2, boxY - 2, boxW + 4, boxH + 4, true);   // outer border
  renderer.fillRect(boxX, boxY, boxW, boxH, false);                    // white fill

  const int textX = boxX + (boxW - textWidth) / 2;
  const int textY = boxY + paddingY - 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, upper.c_str(), true, EpdFontFamily::REGULAR);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

}  // namespace TransitionFeedback
