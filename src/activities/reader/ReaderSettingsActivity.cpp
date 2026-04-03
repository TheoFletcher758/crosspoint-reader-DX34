#include "ReaderSettingsActivity.h"

#include <algorithm>

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReadingThemeStore.h"
#include "SettingsList.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const StrId kCategoryNames[] = {StrId::STR_CAT_READER, StrId::STR_STATUS_BAR};

std::string fontSizeValueLabel(const uint8_t family, const uint8_t fontSize) {
  return std::to_string(
      CrossPointSettings::fontSizeToPointSize(family, fontSize));
}

int getValueEditHoldStep(const MappedInputManager& mappedInput,
                         const SettingInfo&) {
  return mappedInput.getHeldTime() >= 1200 ? 5 : 1;
}

int readerFontIdFor(const uint8_t family, const uint8_t fontSize) {
  const uint8_t normalizedFontSize =
      CrossPointSettings::normalizeFontSizeForFamily(family, fontSize);

  switch (normalizedFontSize) {
    case CrossPointSettings::SIZE_13:
      return CHAREINK_13_FONT_ID;
    case CrossPointSettings::SIZE_14:
      return CHAREINK_14_FONT_ID;
    case CrossPointSettings::MEDIUM:
      return CHAREINK_15_FONT_ID;
    case CrossPointSettings::SIZE_16:
      return CHAREINK_16_FONT_ID;
    case CrossPointSettings::LARGE:
      return CHAREINK_17_FONT_ID;
    case CrossPointSettings::SIZE_18:
      return CHAREINK_18_FONT_ID;
    case CrossPointSettings::X_LARGE:
    default:
      return CHAREINK_19_FONT_ID;
    }
}
}  // namespace

bool ReaderSettingsActivity::isTxtContext() const {
  return bookCachePath.find("/txt_") != std::string::npos;
}

void ReaderSettingsActivity::persistSettings(const char* context) {
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("RSET", "Failed to save settings (%s)", context);
    return;
  }

  dirty = true;
  if (!bookCachePath.empty() &&
      !READING_THEMES.saveCurrentBookSettings(bookCachePath)) {
    LOG_ERR("RSET", "Failed to save book settings (%s)", context);
  }
}

const std::vector<SettingInfo>* ReaderSettingsActivity::settingsForCategory(
    const int categoryIndex) const {
  switch (categoryIndex) {
    case 0:
      return &readerSettings;
    case 1:
    default:
      return &statusBarSettings;
  }
}

int ReaderSettingsActivity::findNextEditableRow(const int startIndex,
                                                const int direction) const {
  if (flatRows.empty()) {
    return 0;
  }
  int idx = startIndex;
  for (size_t i = 0; i < flatRows.size(); i++) {
    idx = (direction > 0)
              ? ButtonNavigator::nextIndex(idx, static_cast<int>(flatRows.size()))
              : ButtonNavigator::previousIndex(
                    idx, static_cast<int>(flatRows.size()));
    if (!flatRows[idx].isHeader) {
      return idx;
    }
  }
  return startIndex;
}

bool ReaderSettingsActivity::isPopupValueSetting(
    const SettingInfo& setting) const {
  if (setting.type != SettingType::VALUE || setting.valuePtr == nullptr) {
    return false;
  }
  return setting.valuePtr == &CrossPointSettings::lineSpacingPercent ||
         setting.valuePtr == &CrossPointSettings::screenMarginHorizontal ||
         setting.valuePtr == &CrossPointSettings::screenMarginTop ||
         setting.valuePtr == &CrossPointSettings::screenMarginBottom;
}

void ReaderSettingsActivity::startFontSizeEdit() {
  fontSizeEditMode = true;
  fontSizeEditDraftIndex = CrossPointSettings::fontSizeToDisplayIndex(
      SETTINGS.fontFamily, SETTINGS.fontSize);
}

void ReaderSettingsActivity::adjustFontSizeEdit(const int delta) {
  const int optionCount = CrossPointSettings::fontSizeOptionCount(SETTINGS.fontFamily);
  const int next = static_cast<int>(fontSizeEditDraftIndex) + delta;
  fontSizeEditDraftIndex = static_cast<uint8_t>(
      std::clamp(next, 0, std::max(0, optionCount - 1)));
}

void ReaderSettingsActivity::applyFontSizeEdit() {
  SETTINGS.fontSize = CrossPointSettings::displayIndexToFontSize(
      SETTINGS.fontFamily, fontSizeEditDraftIndex);
  fontSizeEditMode = false;
  persistSettings("reader settings font size");
}

void ReaderSettingsActivity::startValueEdit(const SettingInfo& setting,
                                            const int categoryIndex,
                                            const int settingIndex) {
  valueEditMode = true;
  valueEditCategoryIndex = categoryIndex;
  valueEditSettingIndex = settingIndex;
  valueEditMin = setting.valueRange.min;
  valueEditMax = setting.valueRange.max;
  valueEditDraft =
      std::clamp(SETTINGS.*(setting.valuePtr), valueEditMin, valueEditMax);
}

void ReaderSettingsActivity::adjustValueEdit(const int delta) {
  const int next = static_cast<int>(valueEditDraft) + delta;
  valueEditDraft = static_cast<uint8_t>(std::clamp(
      next, static_cast<int>(valueEditMin), static_cast<int>(valueEditMax)));
}

void ReaderSettingsActivity::applyValueEdit() {
  if (!valueEditMode) {
    return;
  }
  const auto* settings = settingsForCategory(valueEditCategoryIndex);
  if (!settings || valueEditSettingIndex < 0 ||
      valueEditSettingIndex >= static_cast<int>(settings->size())) {
    valueEditMode = false;
    return;
  }

  const auto& setting = (*settings)[valueEditSettingIndex];
  SETTINGS.*(setting.valuePtr) = valueEditDraft;
  valueEditMode = false;
  persistSettings("reader settings value");
}

std::string ReaderSettingsActivity::currentValueEditText() const {
  if (!valueEditMode) {
    return {};
  }
  const auto* settings = settingsForCategory(valueEditCategoryIndex);
  if (!settings || valueEditSettingIndex < 0 ||
      valueEditSettingIndex >= static_cast<int>(settings->size())) {
    return {};
  }

  std::string valueText = std::to_string(valueEditDraft);
  const auto& setting = (*settings)[valueEditSettingIndex];
  if (setting.valuePtr == &CrossPointSettings::lineSpacingPercent) {
    valueText += "%";
  }
  return valueText;
}

void ReaderSettingsActivity::buildSettingsList() {
  readerSettings.clear();
  statusBarSettings.clear();
  flatRows.clear();

  for (auto& setting : getSettingsList()) {
    if (setting.category == StrId::STR_CAT_READER) {
      if (setting.valuePtr == &CrossPointSettings::orientation ||
          setting.valuePtr == &CrossPointSettings::debugBorders ||
          setting.valuePtr == &CrossPointSettings::textAntiAliasing) {
        continue;
      }
      if (isTxtContext() &&
          setting.valuePtr == &CrossPointSettings::paragraphAlignment) {
        setting.enumValues.resize(4);
        if (SETTINGS.paragraphAlignment ==
            CrossPointSettings::BOOK_STYLE) {
          SETTINGS.paragraphAlignment = CrossPointSettings::JUSTIFIED;
        }
      }
      if (isTxtContext() &&
          setting.valuePtr == &CrossPointSettings::readerStyleMode) {
        continue;
      }
      readerSettings.push_back(std::move(setting));
    } else if (setting.category == StrId::STR_STATUS_BAR) {
      statusBarSettings.push_back(std::move(setting));
    }
  }

  if (!isTxtContext()) {
    readerSettings.push_back(SettingInfo::Toggle(
        StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled));
  }

  if (CrossPointSettings::isSingleSizeFontFamily(SETTINGS.fontFamily)) {
    readerSettings.erase(
        std::remove_if(readerSettings.begin(), readerSettings.end(),
                       [](const SettingInfo& setting) {
                         return setting.valuePtr == &CrossPointSettings::fontSize;
                       }),
        readerSettings.end());
  }

  for (int categoryIndex = 0; categoryIndex < 2; categoryIndex++) {
    flatRows.push_back(
        FlatSettingRow{.isHeader = true, .categoryIndex = categoryIndex});
    const auto* settings = settingsForCategory(categoryIndex);
    for (size_t i = 0; i < settings->size(); i++) {
      flatRows.push_back(FlatSettingRow{.isHeader = false,
                                        .categoryIndex = categoryIndex,
                                        .settingIndex = static_cast<int>(i)});
    }
  }
}

void ReaderSettingsActivity::onEnter() {
  Activity::onEnter();
  buildSettingsList();
  selectedRowIndex = findNextEditableRow(0, +1);
  requestUpdate();
}

void ReaderSettingsActivity::onExit() {
  Activity::onExit();
  fontSizeEditMode = false;
  valueEditMode = false;
}

void ReaderSettingsActivity::toggleCurrentSetting() {
  if (selectedRowIndex < 0 || selectedRowIndex >= static_cast<int>(flatRows.size())) {
    return;
  }
  const auto& row = flatRows[selectedRowIndex];
  if (row.isHeader) {
    return;
  }

  const auto* settings = settingsForCategory(row.categoryIndex);
  if (!settings || row.settingIndex < 0 ||
      row.settingIndex >= static_cast<int>(settings->size())) {
    return;
  }

  const auto& setting = (*settings)[row.settingIndex];
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    SETTINGS.*(setting.valuePtr) = !(SETTINGS.*(setting.valuePtr));
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    if (setting.valuePtr == &CrossPointSettings::fontSize) {
      startFontSizeEdit();
      return;
    } else if (setting.valuePtr == &CrossPointSettings::fontFamily) {
      const uint8_t currentIndex =
          CrossPointSettings::fontFamilyToDisplayIndex(SETTINGS.fontFamily);
      SETTINGS.fontFamily = CrossPointSettings::displayIndexToFontFamily(
          (currentIndex + 1) % static_cast<uint8_t>(setting.enumValues.size()));
      SETTINGS.fontFamily =
          CrossPointSettings::normalizeFontFamily(SETTINGS.fontFamily);
      SETTINGS.fontSize = CrossPointSettings::normalizeFontSizeForFamily(
          SETTINGS.fontFamily, SETTINGS.fontSize);
      SETTINGS.lineSpacingPercent =
          CrossPointSettings::defaultLineSpacingPercentForFamily(
              SETTINGS.fontFamily, SETTINGS.lineSpacingPercent);
      buildSettingsList();
      selectedRowIndex =
          std::min(selectedRowIndex, static_cast<int>(flatRows.size()) - 1);
    } else {
      const int currentValue = SETTINGS.*(setting.valuePtr);
      SETTINGS.*(setting.valuePtr) =
          (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
    }
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (isPopupValueSetting(setting)) {
      startValueEdit(setting, row.categoryIndex, row.settingIndex);
      return;
    }
    const int currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else {
    return;
  }

  persistSettings("reader settings toggle");
}

void ReaderSettingsActivity::loop() {
  if (fontSizeEditMode) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      fontSizeEditMode = false;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      applyFontSizeEdit();
      requestUpdate();
      return;
    }

    buttonNavigator.onNextRelease([this] {
      adjustFontSizeEdit(+1);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this] {
      adjustFontSizeEdit(-1);
      requestUpdate();
    });
    buttonNavigator.onNextContinuous([this] {
      adjustFontSizeEdit(+1);
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this] {
      adjustFontSizeEdit(-1);
      requestUpdate();
    });
    return;
  }

  if (valueEditMode) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      valueEditMode = false;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      applyValueEdit();
      requestUpdate();
      return;
    }

    buttonNavigator.onNextRelease([this] {
      adjustValueEdit(+1);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this] {
      adjustValueEdit(-1);
      requestUpdate();
    });
    buttonNavigator.onNextContinuous([this] {
      const auto* settings = settingsForCategory(valueEditCategoryIndex);
      if (!settings || valueEditSettingIndex < 0 ||
          valueEditSettingIndex >= static_cast<int>(settings->size())) {
        return;
      }
      adjustValueEdit(+getValueEditHoldStep(mappedInput,
                                            (*settings)[valueEditSettingIndex]));
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this] {
      const auto* settings = settingsForCategory(valueEditCategoryIndex);
      if (!settings || valueEditSettingIndex < 0 ||
          valueEditSettingIndex >= static_cast<int>(settings->size())) {
        return;
      }
      adjustValueEdit(-getValueEditHoldStep(mappedInput,
                                            (*settings)[valueEditSettingIndex]));
      requestUpdate();
    });
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onClose(dirty);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedRowIndex = findNextEditableRow(selectedRowIndex, +1);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedRowIndex = findNextEditableRow(selectedRowIndex, -1);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this] {
    selectedRowIndex = findNextEditableRow(selectedRowIndex, +1);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this] {
    selectedRowIndex = findNextEditableRow(selectedRowIndex, -1);
    requestUpdate();
  });
}

void ReaderSettingsActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto metrics = UITheme::getInstance().getMetrics();

  renderer.drawCenteredText(UI_12_FONT_ID, metrics.topPadding + 5,
                            tr(STR_READER_SETTINGS), true,
                            EpdFontFamily::REGULAR);

  const int contentY = metrics.topPadding + metrics.headerHeight;
  const int contentHeight =
      pageHeight - (contentY + metrics.buttonHintsHeight + metrics.verticalSpacing);
  const int rowHeight = metrics.listRowHeight;
  const int pageItems = std::max(1, contentHeight / rowHeight);
  const int pageStartIndex = (selectedRowIndex / pageItems) * pageItems;

  for (int i = pageStartIndex;
       i < static_cast<int>(flatRows.size()) && i < pageStartIndex + pageItems;
       i++) {
    const int rowY = contentY + (i - pageStartIndex) * rowHeight;
    const auto& row = flatRows[i];

    if (row.isHeader) {
      renderer.fillRect(0, rowY, pageWidth, rowHeight, true);
      const char* label = I18N.get(kCategoryNames[row.categoryIndex]);
      const int textW =
          renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, (pageWidth - textW) / 2, rowY, label,
                        false, EpdFontFamily::REGULAR);
      continue;
    }

    const auto* settings = settingsForCategory(row.categoryIndex);
    const auto& setting = (*settings)[row.settingIndex];
    const bool isSelected = (i == selectedRowIndex);
    const char* settingName = I18N.get(setting.nameId);
    constexpr int kChipPad = 1;
    const int textH = renderer.getTextHeight(UI_10_FONT_ID);
    const int chipH = textH + kChipPad * 2;
    const int chipY = rowY + (rowHeight - chipH) / 2;

    if (isSelected) {
      const int nameWidth = renderer.getTextWidth(UI_10_FONT_ID, settingName);
      renderer.fillRect(metrics.contentSidePadding - kChipPad, chipY,
                        nameWidth + kChipPad * 2, chipH, true);
    }
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, rowY,
                      settingName, !isSelected);

    std::string valueText;
    if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
      valueText =
          (SETTINGS.*(setting.valuePtr)) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
      if (setting.valuePtr == &CrossPointSettings::fontSize) {
        valueText = fontSizeValueLabel(SETTINGS.fontFamily, SETTINGS.fontSize);
      } else if (setting.valuePtr == &CrossPointSettings::fontFamily) {
        valueText = I18N.get(
            setting.enumValues[CrossPointSettings::fontFamilyToDisplayIndex(
                SETTINGS.fontFamily)]);
      } else {
        valueText = I18N.get(setting.enumValues[SETTINGS.*(setting.valuePtr)]);
      }
    } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
      const uint8_t valueToShow =
          (valueEditMode && row.categoryIndex == valueEditCategoryIndex &&
           row.settingIndex == valueEditSettingIndex)
              ? valueEditDraft
              : SETTINGS.*(setting.valuePtr);
      valueText = std::to_string(valueToShow);
      if (setting.valuePtr == &CrossPointSettings::lineSpacingPercent) {
        valueText += "%";
      }
    }

    if (!valueText.empty()) {
      const int valueW = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
      const int valueX = pageWidth - metrics.contentSidePadding - valueW;
      if (isSelected) {
        renderer.fillRect(valueX - kChipPad, chipY, valueW + kChipPad * 2, chipH, true);
      }
      renderer.drawText(UI_10_FONT_ID, valueX, rowY,
                        valueText.c_str(), !isSelected);
    }
  }

  const char* confirmLabel =
      (valueEditMode || fontSizeEditMode) ? tr(STR_CONFIRM) : tr(STR_TOGGLE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel,
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                      labels.btn4);

  if (fontSizeEditMode) {
    const int popupW = std::min(pageWidth - 30, 280);
    const int popupH = 176;
    const int popupX = (pageWidth - popupW) / 2;
    const int popupY = (pageHeight - popupH) / 2;
    const int optionCount =
        CrossPointSettings::fontSizeOptionCount(SETTINGS.fontFamily);
    const int draftIndex = static_cast<int>(fontSizeEditDraftIndex);
    const int prevIndex = std::max(0, draftIndex - 1);
    const int nextIndex = std::min(optionCount - 1, draftIndex + 1);
    const uint8_t prevFontSize = CrossPointSettings::displayIndexToFontSize(
        SETTINGS.fontFamily, static_cast<uint8_t>(prevIndex));
    const uint8_t currentFontSize = CrossPointSettings::displayIndexToFontSize(
        SETTINGS.fontFamily, fontSizeEditDraftIndex);
    const uint8_t nextFontSize = CrossPointSettings::displayIndexToFontSize(
        SETTINGS.fontFamily, static_cast<uint8_t>(nextIndex));

    renderer.fillRect(popupX - 2, popupY - 2, popupW + 4, popupH + 4, true);
    renderer.fillRect(popupX, popupY, popupW, popupH, false);
    renderer.drawCenteredText(UI_10_FONT_ID, popupY + 8, tr(STR_FONT_SIZE));

    if (prevIndex != draftIndex) {
      const std::string prevLabel =
          fontSizeValueLabel(SETTINGS.fontFamily, prevFontSize);
      const int prevFontId = readerFontIdFor(SETTINGS.fontFamily, prevFontSize);
      const int prevTextW = renderer.getTextWidth(prevFontId, prevLabel.c_str());
      renderer.drawText(prevFontId, popupX + (popupW - prevTextW) / 2,
                        popupY + 42, prevLabel.c_str(), true);
    }

    const std::string currentLabel =
        fontSizeValueLabel(SETTINGS.fontFamily, currentFontSize);
    const int currentFontId =
        readerFontIdFor(SETTINGS.fontFamily, currentFontSize);
    const int currentTextW =
        renderer.getTextWidth(currentFontId, currentLabel.c_str());
    renderer.drawText(currentFontId, popupX + (popupW - currentTextW) / 2,
                      popupY + 80, currentLabel.c_str(), true);

    if (nextIndex != draftIndex) {
      const std::string nextLabel =
          fontSizeValueLabel(SETTINGS.fontFamily, nextFontSize);
      const int nextFontId = readerFontIdFor(SETTINGS.fontFamily, nextFontSize);
      const int nextTextW = renderer.getTextWidth(nextFontId, nextLabel.c_str());
      renderer.drawText(nextFontId, popupX + (popupW - nextTextW) / 2,
                        popupY + 122, nextLabel.c_str(), true);
    }
  }

  if (valueEditMode) {
    const auto* settings = settingsForCategory(valueEditCategoryIndex);
    if (settings && valueEditSettingIndex >= 0 &&
        valueEditSettingIndex < static_cast<int>(settings->size())) {
      const auto& setting = (*settings)[valueEditSettingIndex];
      const char* settingLabel = I18N.get(setting.nameId);
      const std::string valueText = currentValueEditText();
      const int popupW = std::min(pageWidth - 30, 300);
      const int popupH = 86;
      const int popupX = (pageWidth - popupW) / 2;
      const int popupY = (pageHeight - popupH) / 2;

      renderer.fillRect(popupX - 2, popupY - 2, popupW + 4, popupH + 4, true);
      renderer.fillRect(popupX, popupY, popupW, popupH, false);

      const int titleW = renderer.getTextWidth(UI_10_FONT_ID, settingLabel);
      renderer.drawText(UI_10_FONT_ID, popupX + (popupW - titleW) / 2,
                        popupY + 8, settingLabel, true);

      const int valueW = renderer.getTextWidth(UI_12_FONT_ID, valueText.c_str());
      renderer.drawText(UI_12_FONT_ID, popupX + (popupW - valueW) / 2,
                        popupY + 30, valueText.c_str(), true);

      const int barX = popupX + 20;
      const int barY = popupY + popupH - 22;
      const int barW = popupW - 40;
      const int barH = 8;
      renderer.drawRect(barX, barY, barW, barH, true);
      const int range =
          std::max(1, static_cast<int>(valueEditMax) - static_cast<int>(valueEditMin));
      const int filledW =
          2 + ((static_cast<int>(valueEditDraft) -
                static_cast<int>(valueEditMin)) *
               std::max(1, barW - 4)) /
                  range;
      renderer.fillRect(barX + 2, barY + 2, filledW, std::max(1, barH - 4),
                        true);
    }
  }

  renderer.displayBuffer();
}
