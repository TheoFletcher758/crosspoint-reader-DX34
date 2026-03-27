#include "ConfirmDialogActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kOptionCount = 2;
}

void ConfirmDialogActivity::onEnter() {
  Activity::onEnter();
  selectedOptionIndex = 1;  // Default to Cancel
  requestUpdate();
}

void ConfirmDialogActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedOptionIndex = (selectedOptionIndex + 1) % kOptionCount;
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedOptionIndex = (selectedOptionIndex + kOptionCount - 1) % kOptionCount;
    requestUpdate();
  });

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedOptionIndex == 0) {
      onConfirm();
    } else {
      onCancel();
    }
  }
}

void ConfirmDialogActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const char* options[] = {tr(STR_CONFIRM), tr(STR_CANCEL)};
  constexpr int rowH = 28;
  const int popupW = pageWidth - 48;

  // Wrap message text to fit popup width
  const int textMaxW = popupW - 24;
  const std::string truncatedMsg = renderer.truncatedText(UI_10_FONT_ID, message.c_str(), textMaxW);
  const int textH = renderer.getLineHeight(UI_10_FONT_ID);

  const int popupH = 20 + textH + 16 + kOptionCount * rowH;
  const int popupX = (pageWidth - popupW) / 2;
  const int popupY = (pageHeight - popupH) / 2;

  renderer.fillRect(popupX - 2, popupY - 2, popupW + 4, popupH + 4, true);
  renderer.fillRect(popupX, popupY, popupW, popupH, false);

  // Draw message
  renderer.drawText(UI_10_FONT_ID, popupX + 12, popupY + 10, truncatedMsg.c_str(), true);

  // Draw options
  const int optionsStartY = popupY + 20 + textH + 8;
  for (int i = 0; i < kOptionCount; i++) {
    const int rowY = optionsStartY + i * rowH;
    const bool selected = (i == selectedOptionIndex);
    if (selected) {
      renderer.fillRect(popupX + 6, rowY - 1, popupW - 12, rowH - 2, true);
    }
    renderer.drawText(UI_10_FONT_ID, popupX + 12, rowY, options[i], !selected);
  }

  renderer.displayBuffer();
}
