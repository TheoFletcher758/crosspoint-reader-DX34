#include "SettingsActivity.h"

#include <algorithm>
#include <GfxRenderer.h>
#include <Logging.h>

#include <HalStorage.h>

#include "ButtonRemapActivity.h"
#include "CalibreSettingsActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderSettingsActivity.h"
#include "MappedInputManager.h"
#include "OtaUpdateActivity.h"
#include "SettingsList.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"

const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                              StrId::STR_STATUS_BAR, StrId::STR_CAT_CONTROLS,
                                                              StrId::STR_CAT_SYSTEM};

namespace {
constexpr unsigned long doubleTapMs = 350;

uint8_t nextReaderMarginValue(const uint8_t current) {
  static constexpr uint8_t kMargins[] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55};
  for (size_t i = 0; i < sizeof(kMargins) / sizeof(kMargins[0]); i++) {
    if (current < kMargins[i]) {
      return kMargins[i];
    }
    if (current == kMargins[i]) {
      return kMargins[(i + 1) % (sizeof(kMargins) / sizeof(kMargins[0]))];
    }
  }
  return kMargins[0];
}

void persistSettingsWithLog(const char* context) {
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("SET", "Failed to save settings (%s)", context);
  }
}

const char* fontSizeValueLabel(const uint8_t family, const uint8_t fontSize) {
  switch (CrossPointSettings::fontSizeToPointSize(family, fontSize)) {
    case 13:
      return "13";
    case 14:
      return "14";
    case 15:
      return "15";
    case 16:
      return "16";
    case 17:
      return "17";
    case 18:
      return "18";
    default:
      return "16";
  }
}

}

const std::vector<SettingInfo>* SettingsActivity::settingsForCategory(const int categoryIndex) const {
  switch (categoryIndex) {
    case 0:
      return &displaySettings;
    case 1:
      return &readerSettings;
    case 2:
      return &statusBarSettings;
    case 3:
      return &controlsSettings;
    case 4:
    default:
      return &systemSettings;
  }
}

int SettingsActivity::findNextEditableRow(const int startIndex, const int direction) const {
  if (flatRows.empty()) {
    return 0;
  }
  int idx = startIndex;
  for (size_t i = 0; i < flatRows.size(); i++) {
    idx = (direction > 0) ? ButtonNavigator::nextIndex(idx, static_cast<int>(flatRows.size()))
                          : ButtonNavigator::previousIndex(idx, static_cast<int>(flatRows.size()));
    if (!flatRows[idx].isHeader) {
      return idx;
    }
  }
  return startIndex;
}

void SettingsActivity::jumpCategory(const int direction) {
  if (categoryHeaderRowIndices.empty()) {
    return;
  }
  int currentCategory = flatRows[selectedRowIndex].categoryIndex;
  int targetCategory = currentCategory;
  for (int i = 0; i < categoryCount; i++) {
    targetCategory = (direction > 0) ? ButtonNavigator::nextIndex(targetCategory, categoryCount)
                                     : ButtonNavigator::previousIndex(targetCategory, categoryCount);
    const auto* settings = settingsForCategory(targetCategory);
    if (settings && !settings->empty()) {
      break;
    }
  }
  const int headerRow = categoryHeaderRowIndices[targetCategory];
  selectedRowIndex = findNextEditableRow(headerRow, +1);
}

bool SettingsActivity::isPopupValueSetting(const SettingInfo& setting) const {
  if (setting.type != SettingType::VALUE || setting.valuePtr == nullptr) {
    return false;
  }
  return setting.valuePtr == &CrossPointSettings::lineSpacingPercent ||
         setting.valuePtr == &CrossPointSettings::screenMarginHorizontal ||
         setting.valuePtr == &CrossPointSettings::screenMarginTop ||
         setting.valuePtr == &CrossPointSettings::screenMarginBottom;
}

void SettingsActivity::startValueEdit(const SettingInfo& setting, const int categoryIndex, const int settingIndex) {
  valueEditMode = true;
  valueEditCategoryIndex = categoryIndex;
  valueEditSettingIndex = settingIndex;
  valueEditMin = setting.valueRange.min;
  valueEditMax = setting.valueRange.max;
  valueEditOriginal = SETTINGS.*(setting.valuePtr);
  valueEditDraft = std::clamp(valueEditOriginal, valueEditMin, valueEditMax);
}

void SettingsActivity::adjustValueEdit(const int delta) {
  const int next = static_cast<int>(valueEditDraft) + delta;
  valueEditDraft = static_cast<uint8_t>(std::clamp(next, static_cast<int>(valueEditMin), static_cast<int>(valueEditMax)));
}

namespace {
int getValueEditHoldStep(const MappedInputManager& mappedInput,
                         const SettingInfo&) {
  return mappedInput.getHeldTime() >= 1200 ? 5 : 1;
}
}

void SettingsActivity::applyValueEdit() {
  if (!valueEditMode) {
    return;
  }
  const auto* settings = settingsForCategory(valueEditCategoryIndex);
  if (!settings || valueEditSettingIndex < 0 || valueEditSettingIndex >= static_cast<int>(settings->size())) {
    cancelValueEdit();
    return;
  }

  const auto& setting = (*settings)[valueEditSettingIndex];
  SETTINGS.*(setting.valuePtr) = valueEditDraft;
  if (SETTINGS.uniformMargins &&
      setting.valuePtr == &CrossPointSettings::screenMarginHorizontal) {
    SETTINGS.screenMarginTop = valueEditDraft;
    SETTINGS.screenMarginBottom = valueEditDraft;
  }

  persistSettingsWithLog("settings value confirm");
  valueEditMode = false;
  valueEditCategoryIndex = -1;
  valueEditSettingIndex = -1;
}

void SettingsActivity::cancelValueEdit() {
  valueEditMode = false;
  valueEditCategoryIndex = -1;
  valueEditSettingIndex = -1;
}

std::string SettingsActivity::currentValueEditText() const {
  if (!valueEditMode) {
    return {};
  }
  const auto* settings = settingsForCategory(valueEditCategoryIndex);
  if (!settings || valueEditSettingIndex < 0 || valueEditSettingIndex >= static_cast<int>(settings->size())) {
    return {};
  }
  const auto& setting = (*settings)[valueEditSettingIndex];
  std::string v = std::to_string(valueEditDraft);
  if (setting.valuePtr == &CrossPointSettings::lineSpacingPercent) {
    v += "%";
  }
  return v;
}

void SettingsActivity::buildSettingsList() {
  displaySettings.clear();
  readerSettings.clear();
  statusBarSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();
  flatRows.clear();
  categoryHeaderRowIndices.clear();

  for (auto& setting : getSettingsList()) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      displaySettings.push_back(std::move(setting));
    } else if (setting.category == StrId::STR_CAT_READER) {
      readerSettings.push_back(std::move(setting));
    } else if (setting.category == StrId::STR_STATUS_BAR) {
      statusBarSettings.push_back(std::move(setting));
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      controlsSettings.push_back(std::move(setting));
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      systemSettings.push_back(std::move(setting));
    }
    // Web-only categories (KOReader Sync, OPDS Browser) are skipped for device UI
  }

  // Filter margin entries based on uniform/separate mode.
  {
    const bool uniform = SETTINGS.uniformMargins;
    readerSettings.erase(
        std::remove_if(readerSettings.begin(), readerSettings.end(),
                       [uniform](const SettingInfo& s) {
                         if (uniform) {
                           // Uniform: hide separate margin entries
                           return s.nameId == StrId::STR_SCREEN_MARGIN_HORIZONTAL ||
                                  s.nameId == StrId::STR_SCREEN_MARGIN_TOP ||
                                  s.nameId == StrId::STR_SCREEN_MARGIN_BOTTOM;
                         } else {
                           // Separate: hide uniform margin entry
                           return s.nameId == StrId::STR_SCREEN_MARGIN;
                         }
                       }),
        readerSettings.end());
  }

  displaySettings.push_back(SettingInfo::Action(StrId::STR_RANDOMIZE_SLEEP_IMAGES, SettingAction::RandomizeSleepImages));

  // Append device-only ACTION items
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_BROWSER, SettingAction::OPDSBrowser));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_REFRESH_HOME_STATS, SettingAction::RefreshHomeStats));

  categoryHeaderRowIndices.resize(categoryCount, 0);
  for (int c = 0; c < categoryCount; c++) {
    categoryHeaderRowIndices[c] = static_cast<int>(flatRows.size());
    flatRows.push_back(FlatSettingRow{.isHeader = true, .categoryIndex = c, .settingIndex = -1});
    const auto* settings = settingsForCategory(c);
    for (size_t i = 0; i < settings->size(); i++) {
      flatRows.push_back(FlatSettingRow{.isHeader = false, .categoryIndex = c, .settingIndex = static_cast<int>(i)});
    }
  }
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  buildSettingsList();
  selectedRowIndex = findNextEditableRow(0, +1);
  lastNextTapMs = 0;
  lastPreviousTapMs = 0;

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();
  cancelValueEdit();
  persistSettingsWithLog("settings exit");
}

void SettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  const auto dismissPopupOnAnyPress = [this](bool& popupOpen) {
    const bool anyPress = mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Back) ||
                          mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                          mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Left) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Right) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Power);
    if (anyPress) {
      popupOpen = false;
      requestUpdate();
      return true;
    }
    return false;
  };

  if (messagePopupOpen) {
    if (dismissPopupOnAnyPress(messagePopupOpen)) {
      messagePopupText.clear();
    }
    return;
  }

  if (homeStatsScanning) {
    // "Scanning..." was rendered on previous frame. Now do the actual scan.
    BaseTheme::refreshHomeInfoStats();
    homeStatsScanning = false;
    homeStatsPopupOpen = true;
    requestUpdate();
    return;
  }

  if (homeStatsPopupOpen) {
    if (dismissPopupOnAnyPress(homeStatsPopupOpen)) {
      BaseTheme::invalidateHomeInfoStats();
      renderer.requestFullRefresh();
    }
    return;
  }

  if (randomizePopupOpen) {
    dismissPopupOnAnyPress(randomizePopupOpen);
    return;
  }

  if (valueEditMode) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelValueEdit();
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

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectedRowIndex > 0) {
      selectedRowIndex = 0;
      requestUpdate();
    } else {
      persistSettingsWithLog("settings back");
      onGoHome();
    }
    return;
  }

  // Handle navigation
  buttonNavigator.onNextRelease([this] {
    const unsigned long now = millis();
    if (lastNextTapMs > 0 && now - lastNextTapMs <= doubleTapMs) {
      jumpCategory(+1);
      lastNextTapMs = 0;
    } else {
      selectedRowIndex = findNextEditableRow(selectedRowIndex, +1);
      lastNextTapMs = now;
    }
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    const unsigned long now = millis();
    if (lastPreviousTapMs > 0 && now - lastPreviousTapMs <= doubleTapMs) {
      jumpCategory(-1);
      lastPreviousTapMs = 0;
    } else {
      selectedRowIndex = findNextEditableRow(selectedRowIndex, -1);
      lastPreviousTapMs = now;
    }
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

void SettingsActivity::toggleCurrentSetting() {
  if (selectedRowIndex < 0 || selectedRowIndex >= static_cast<int>(flatRows.size())) {
    return;
  }
  const auto& row = flatRows[selectedRowIndex];
  if (row.isHeader || row.settingIndex < 0) {
    return;
  }
  const auto* settings = settingsForCategory(row.categoryIndex);
  if (!settings || row.settingIndex >= static_cast<int>(settings->size())) {
    return;
  }

  const auto& setting = (*settings)[row.settingIndex];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
    if (setting.valuePtr == &CrossPointSettings::uniformMargins) {
      if (SETTINGS.uniformMargins) {
        SETTINGS.screenMarginTop = SETTINGS.screenMarginHorizontal;
        SETTINGS.screenMarginBottom = SETTINGS.screenMarginHorizontal;
      }
      buildSettingsList();
      selectedRowIndex = std::min(selectedRowIndex, static_cast<int>(flatRows.size()) - 1);
    }
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (setting.valuePtr == &CrossPointSettings::fontSize) {
      SETTINGS.fontSize =
          CrossPointSettings::nextFontSize(SETTINGS.fontFamily, currentValue);
    } else if (setting.valuePtr == &CrossPointSettings::fontFamily) {
      const uint8_t currentIndex =
          CrossPointSettings::fontFamilyToDisplayIndex(SETTINGS.fontFamily);
      SETTINGS.fontFamily = CrossPointSettings::displayIndexToFontFamily(
          (currentIndex + 1) % static_cast<uint8_t>(setting.enumValues.size()));
    } else {
      SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
    }
    if (setting.valuePtr == &CrossPointSettings::fontFamily) {
      SETTINGS.fontFamily =
          CrossPointSettings::normalizeFontFamily(SETTINGS.fontFamily);
      SETTINGS.fontSize = CrossPointSettings::normalizeFontSizeForFamily(
          SETTINGS.fontFamily, SETTINGS.fontSize);
      SETTINGS.lineSpacingPercent = 90;  // Reset to default on font change
      buildSettingsList();
      selectedRowIndex = std::min(selectedRowIndex, static_cast<int>(flatRows.size()) - 1);
    }
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (isPopupValueSetting(setting)) {
      startValueEdit(setting, row.categoryIndex, row.settingIndex);
      return;
    }
    if (setting.valuePtr == &CrossPointSettings::screenMarginHorizontal) {
      SETTINGS.*(setting.valuePtr) = nextReaderMarginValue(SETTINGS.*(setting.valuePtr));
      if (SETTINGS.uniformMargins) {
        SETTINGS.screenMarginTop = SETTINGS.screenMarginHorizontal;
        SETTINGS.screenMarginBottom = SETTINGS.screenMarginHorizontal;
      }
    } else {
      const int currentValue = SETTINGS.*(setting.valuePtr);
      if (currentValue < setting.valueRange.min ||
          currentValue > setting.valueRange.max) {
        SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
      } else if (currentValue + setting.valueRange.step > setting.valueRange.max) {
        SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
      } else {
        SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
      }
    }
  } else if (setting.type == SettingType::ACTION) {
    auto enterSubActivity = [this](Activity* activity) {
      exitActivity();
      enterNewActivity(activity);
    };

    auto onComplete = [this] {
      exitActivity();
      requestUpdate();
    };

    auto onCompleteBool = [this](bool) {
      exitActivity();
      requestUpdate();
    };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        enterSubActivity(new ButtonRemapActivity(renderer, mappedInput, onComplete));
        break;
      case SettingAction::KOReaderSync:
        enterSubActivity(new KOReaderSettingsActivity(renderer, mappedInput, onComplete));
        break;
      case SettingAction::OPDSBrowser:
        enterSubActivity(new CalibreSettingsActivity(renderer, mappedInput, onComplete));
        break;
      case SettingAction::Network:
        enterSubActivity(new WifiSelectionActivity(renderer, mappedInput, onCompleteBool, false));
        break;
      case SettingAction::ClearCache:
        enterSubActivity(new ClearCacheActivity(renderer, mappedInput, onComplete));
        break;
      case SettingAction::CheckForUpdates:
        enterSubActivity(new OtaUpdateActivity(renderer, mappedInput, onComplete));
        break;
      case SettingAction::RandomizeSleepImages:
        randomizePopupSuccess = SleepActivity::randomizeSleepImagePlaylist();
        randomizePopupOpen = true;
        requestUpdate();
        break;
      case SettingAction::RefreshHomeStats: {
        homeStatsScanning = true;
        requestUpdate();
        break;
      }
      case SettingAction::None:
        // Do nothing
        break;
    }
  } else {
    return;
  }

  persistSettingsWithLog("settings toggle");
}

void SettingsActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  auto metrics = UITheme::getInstance().getMetrics();

  // Top status line: version left, battery right
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding,
                    metrics.topPadding + 5, CROSSPOINT_VERSION);
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = pageWidth - 12;
  GUI.drawBatteryRight(renderer,
                       Rect{batteryX, metrics.topPadding + 5,
                            metrics.batteryWidth, metrics.batteryHeight},
                       showBatteryPercentage, UI_10_FONT_ID,
                       metrics.batteryWidth, metrics.batteryHeight, false);

  const int contentY = metrics.topPadding + metrics.headerHeight;
  const int contentHeight = pageHeight - (contentY + metrics.buttonHintsHeight + metrics.verticalSpacing);
  const int rowHeight = metrics.listRowHeight;
  const int pageItems = std::max(1, contentHeight / rowHeight);
  const int pageStartIndex = (selectedRowIndex / pageItems) * pageItems;

  for (int i = pageStartIndex; i < static_cast<int>(flatRows.size()) && i < pageStartIndex + pageItems; i++) {
    const int rowY = contentY + (i - pageStartIndex) * rowHeight;
    const auto& row = flatRows[i];

    if (row.isHeader) {
      renderer.fillRect(0, rowY, pageWidth, rowHeight, true);
      const char* label = I18N.get(categoryNames[row.categoryIndex]);
      const int textW = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::REGULAR);
      const int textX = (pageWidth - textW) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, rowY, label, false, EpdFontFamily::REGULAR);
      continue;
    }

    const auto* settings = settingsForCategory(row.categoryIndex);
    const auto& setting = (*settings)[row.settingIndex];
    const int rowFont = UI_10_FONT_ID;
    const bool isSelected = (i == selectedRowIndex);
    const char* settingName = I18N.get(setting.nameId);
    constexpr int kChipPad = 1;
    const int textH = renderer.getTextHeight(rowFont);
    const int chipH = textH + kChipPad * 2;
    const int chipY = rowY + (rowHeight - chipH) / 2;

    if (isSelected) {
      const int nameWidth = renderer.getTextWidth(rowFont, settingName);
      renderer.fillRect(metrics.contentSidePadding - kChipPad, chipY,
                        nameWidth + kChipPad * 2, chipH, true);
    }
    renderer.drawText(rowFont, metrics.contentSidePadding, rowY, settingName, !isSelected);

    std::string valueText;
    if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
      valueText = (SETTINGS.*(setting.valuePtr)) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
      if (setting.valuePtr == &CrossPointSettings::fontSize) {
        valueText = fontSizeValueLabel(SETTINGS.fontFamily, SETTINGS.fontSize);
      } else if (setting.valuePtr == &CrossPointSettings::fontFamily) {
        valueText = I18N.get(setting.enumValues[CrossPointSettings::fontFamilyToDisplayIndex(
            SETTINGS.fontFamily)]);
      } else {
        valueText = I18N.get(setting.enumValues[SETTINGS.*(setting.valuePtr)]);
      }
    } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
      const uint8_t valueToShow =
          (valueEditMode && row.categoryIndex == valueEditCategoryIndex && row.settingIndex == valueEditSettingIndex)
              ? valueEditDraft
              : SETTINGS.*(setting.valuePtr);
      valueText = std::to_string(valueToShow);
      if (setting.valuePtr == &CrossPointSettings::lineSpacingPercent) {
        valueText += "%";
      }
    }

    if (!valueText.empty()) {
      const int valueW = renderer.getTextWidth(rowFont, valueText.c_str());
      const int valueX = pageWidth - metrics.contentSidePadding - valueW;
      if (isSelected) {
        renderer.fillRect(valueX - kChipPad, chipY, valueW + kChipPad * 2, chipH, true);
      }
      renderer.drawText(rowFont, valueX, rowY, valueText.c_str(), !isSelected);
    }
  }

  // Draw help text
  const char* confirmLabel = valueEditMode ? tr(STR_CONFIRM) : tr(STR_TOGGLE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (homeStatsScanning) {
    GUI.drawPopup(renderer, "Scanning SD card...");
  }

  if (homeStatsPopupOpen) {
    GUI.drawHomeInfoStatsPopup(renderer);
  }

  if (randomizePopupOpen) {
    const char* msg = randomizePopupSuccess ? tr(STR_DONE) : tr(STR_NO_ENTRIES);
    GUI.drawPopup(renderer, msg);
  }

  if (messagePopupOpen) {
    GUI.drawPopup(renderer, messagePopupText.c_str());
  }

  if (valueEditMode) {
    const auto* settings = settingsForCategory(valueEditCategoryIndex);
    if (settings && valueEditSettingIndex >= 0 && valueEditSettingIndex < static_cast<int>(settings->size())) {
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
      renderer.drawText(UI_10_FONT_ID, popupX + (popupW - titleW) / 2, popupY + 8, settingLabel, true);

      const int valueW = renderer.getTextWidth(UI_12_FONT_ID, valueText.c_str());
      renderer.drawText(UI_12_FONT_ID, popupX + (popupW - valueW) / 2, popupY + 30, valueText.c_str(), true);

      const int barX = popupX + 20;
      const int barY = popupY + popupH - 22;
      const int barW = popupW - 40;
      const int barH = 8;
      renderer.drawRect(barX, barY, barW, barH, true);
      const int range = std::max(1, static_cast<int>(valueEditMax) - static_cast<int>(valueEditMin));
      const int filledW =
          2 + ((static_cast<int>(valueEditDraft) - static_cast<int>(valueEditMin)) * std::max(1, barW - 4)) / range;
      renderer.fillRect(barX + 2, barY + 2, filledW, std::max(1, barH - 4), true);
    }
  }

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
