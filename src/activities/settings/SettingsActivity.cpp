#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "ButtonRemapActivity.h"
#include "CalibreSettingsActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "KOReaderSettingsActivity.h"
#include "LanguageSelectActivity.h"
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

void SettingsActivity::onEnter() {
  Activity::onEnter();

  // Build per-category vectors from the shared settings list
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

  displaySettings.push_back(SettingInfo::Action(StrId::STR_RANDOMIZE_SLEEP_IMAGES, SettingAction::RandomizeSleepImages));

  // Append device-only ACTION items
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_BROWSER, SettingAction::OPDSBrowser));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
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
  selectedRowIndex = findNextEditableRow(0, +1);
  lastNextTapMs = 0;
  lastPreviousTapMs = 0;

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();
  SETTINGS.saveToFile();
}

void SettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (homeStatsPopupOpen) {
    const bool anyPress = mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Back) ||
                          mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                          mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Left) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Right) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Power);
    if (anyPress) {
      homeStatsPopupOpen = false;
      requestUpdate();
    }
    return;
  }

  if (randomizePopupOpen) {
    const bool anyPress = mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Back) ||
                          mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                          mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Left) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Right) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Power);
    if (anyPress) {
      randomizePopupOpen = false;
      requestUpdate();
    }
    return;
  }

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    onGoHome();
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
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (setting.valuePtr == &CrossPointSettings::screenMarginHorizontal) {
      SETTINGS.*(setting.valuePtr) = nextReaderMarginValue(SETTINGS.*(setting.valuePtr));
    } else if (setting.valuePtr == &CrossPointSettings::screenMarginTop ||
               setting.valuePtr == &CrossPointSettings::screenMarginBottom) {
      const uint8_t next = nextReaderMarginValue(SETTINGS.*(setting.valuePtr));
      SETTINGS.screenMarginTop = next;
      SETTINGS.screenMarginBottom = next;
    } else {
      const int8_t currentValue = SETTINGS.*(setting.valuePtr);
      if (currentValue + setting.valueRange.step > setting.valueRange.max) {
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
      case SettingAction::Language:
        enterSubActivity(new LanguageSelectActivity(renderer, mappedInput, onComplete));
        break;
      case SettingAction::RandomizeSleepImages:
        randomizePopupSuccess = SleepActivity::randomizeSleepImagePlaylist();
        randomizePopupOpen = true;
        requestUpdate();
        break;
      case SettingAction::RefreshHomeStats: {
        homeStatsPopupOpen = true;
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

  SETTINGS.saveToFile();
}

void SettingsActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  auto metrics = UITheme::getInstance().getMetrics();

  // Top status line: version left, battery right
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, metrics.topPadding + 5, CROSSPOINT_VERSION);
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = pageWidth - 12 - metrics.batteryWidth;
  GUI.drawBatteryRight(renderer, Rect{batteryX, metrics.topPadding + 5, metrics.batteryWidth, metrics.batteryHeight},
                       showBatteryPercentage);

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
      const int textW = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::REGULAR);
      const int textX = (pageWidth - textW) / 2;
      renderer.drawText(UI_12_FONT_ID, textX, rowY, label, false, EpdFontFamily::REGULAR);
      continue;
    }

    const auto* settings = settingsForCategory(row.categoryIndex);
    const auto& setting = (*settings)[row.settingIndex];
    const bool black = true;
    const int rowFont = UI_10_FONT_ID;
    const bool isSelected = (i == selectedRowIndex);
    const char* settingName = I18N.get(setting.nameId);

    renderer.drawText(rowFont, metrics.contentSidePadding, rowY, settingName, black);
    if (isSelected) {
      const int nameWidth = renderer.getTextWidth(rowFont, settingName);
      const int underlineY = rowY + renderer.getTextHeight(rowFont) + 1;
      renderer.drawLine(metrics.contentSidePadding, underlineY, metrics.contentSidePadding + nameWidth, underlineY, 3,
                        black);
    }

    std::string valueText;
    if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
      valueText = (SETTINGS.*(setting.valuePtr)) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
      valueText = I18N.get(setting.enumValues[SETTINGS.*(setting.valuePtr)]);
    } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
      valueText = std::to_string(SETTINGS.*(setting.valuePtr));
    }

    if (!valueText.empty()) {
      const int valueW = renderer.getTextWidth(rowFont, valueText.c_str());
      const int valueX = pageWidth - metrics.contentSidePadding - valueW;
      renderer.drawText(rowFont, valueX, rowY, valueText.c_str(), black);
      if (isSelected) {
        const int underlineY = rowY + renderer.getTextHeight(rowFont) + 1;
        renderer.drawLine(valueX, underlineY, valueX + valueW, underlineY, 3, black);
      }
    }
  }

  // Draw help text
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (homeStatsPopupOpen) {
    GUI.drawHomeInfoStatsPopup(renderer);
  }

  if (randomizePopupOpen) {
    const char* msg = randomizePopupSuccess ? tr(STR_DONE) : tr(STR_NO_ENTRIES);
    GUI.drawPopup(renderer, msg);
  }

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
