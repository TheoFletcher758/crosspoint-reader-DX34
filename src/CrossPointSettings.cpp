#include "CrossPointSettings.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <Serialization.h>

#include <cstring>
#include <string>

#include "fontIds.h"

// Initialize the static instance
CrossPointSettings CrossPointSettings::instance;

void readAndValidate(FsFile &file, uint8_t &member, const uint8_t maxValue) {
  uint8_t tempValue;
  serialization::readPod(file, tempValue);
  if (tempValue < maxValue) {
    member = tempValue;
  }
}

namespace {
constexpr uint8_t SETTINGS_FILE_VERSION = 1;
constexpr char SETTINGS_FILE_BIN[] = "/.crosspoint/settings.bin";
constexpr char SETTINGS_FILE_JSON[] = "/.crosspoint/settings.json";
constexpr char SETTINGS_FILE_BAK[] = "/.crosspoint/settings.bin.bak";

// Validate front button mapping to ensure each hardware button is unique.
// If duplicates are detected, reset to the default physical order to prevent
// invalid mappings.
void validateFrontButtonMapping(CrossPointSettings &settings) {
  // Snapshot the logical->hardware mapping so we can compare for duplicates.
  const uint8_t mapping[] = {
      settings.frontButtonBack, settings.frontButtonConfirm,
      settings.frontButtonLeft, settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        // Duplicate detected: restore the default physical order (Back,
        // Confirm, Left, Right).
        settings.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
        settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
        settings.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
        settings.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

// Convert legacy front button layout into explicit logical->hardware mapping.
void applyLegacyFrontButtonLayout(CrossPointSettings &settings) {
  switch (static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(
      settings.frontButtonLayout)) {
  case CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM:
    settings.frontButtonBack = CrossPointSettings::FRONT_HW_LEFT;
    settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_RIGHT;
    settings.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
    settings.frontButtonRight = CrossPointSettings::FRONT_HW_CONFIRM;
    break;
  case CrossPointSettings::LEFT_BACK_CONFIRM_RIGHT:
    settings.frontButtonBack = CrossPointSettings::FRONT_HW_CONFIRM;
    settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_LEFT;
    settings.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
    settings.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
    break;
  case CrossPointSettings::BACK_CONFIRM_RIGHT_LEFT:
    settings.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
    settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
    settings.frontButtonLeft = CrossPointSettings::FRONT_HW_RIGHT;
    settings.frontButtonRight = CrossPointSettings::FRONT_HW_LEFT;
    break;
  case CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT:
  default:
    settings.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
    settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
    settings.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
    settings.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
    break;
  }
}

void migrateLegacyStatusBarMode(CrossPointSettings &settings) {
  settings.statusBarEnabled = 1;
  settings.statusBarShowBattery = 1;
  settings.statusBarShowPageCounter = 0;
  settings.statusBarShowBookPercentage = 0;
  settings.statusBarShowChapterPercentage = 0;
  settings.statusBarShowBookBar = 0;
  settings.statusBarShowChapterBar = 0;
  settings.statusBarShowChapterTitle = 1;
  settings.statusBarTopLine = 0;
  settings.statusBarTextAlignment = CrossPointSettings::STATUS_TEXT_RIGHT;
  settings.statusBarProgressStyle = CrossPointSettings::STATUS_BAR_THICK;

  switch (
      static_cast<CrossPointSettings::STATUS_BAR_MODE>(settings.statusBar)) {
  case CrossPointSettings::STATUS_BAR_MODE::NONE:
    settings.statusBarEnabled = 0;
    settings.statusBarShowBattery = 0;
    settings.statusBarShowChapterTitle = 0;
    break;
  case CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS:
    break;
  case CrossPointSettings::STATUS_BAR_MODE::FULL:
    settings.statusBarShowPageCounter = 1;
    settings.statusBarShowBookPercentage = 1;
    break;
  case CrossPointSettings::STATUS_BAR_MODE::BOOK_PROGRESS_BAR:
    settings.statusBarShowPageCounter = 1;
    settings.statusBarShowBookBar = 1;
    break;
  case CrossPointSettings::STATUS_BAR_MODE::ONLY_BOOK_PROGRESS_BAR:
    settings.statusBarShowBattery = 0;
    settings.statusBarShowChapterTitle = 0;
    settings.statusBarShowBookBar = 1;
    break;
  case CrossPointSettings::STATUS_BAR_MODE::CHAPTER_PROGRESS_BAR:
    settings.statusBarShowBookPercentage = 1;
    settings.statusBarShowChapterBar = 1;
    break;
  default:
    break;
  }
}

uint8_t legacyLineSpacingToPercent(const uint8_t legacy) {
  switch (legacy) {
  case CrossPointSettings::TIGHT:
    return 95;
  case CrossPointSettings::WIDE:
    return 125;
  case CrossPointSettings::NORMAL:
  default:
    return 110;
  }
}
} // namespace

void CrossPointSettings::validateFrontButtonMapping(
    CrossPointSettings &settings) {
  const uint8_t mapping[] = {
      settings.frontButtonBack, settings.frontButtonConfirm,
      settings.frontButtonLeft, settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

bool CrossPointSettings::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveSettings(*this, SETTINGS_FILE_JSON);
}

bool CrossPointSettings::loadFromFile() {
  // Try JSON first
  String json = JsonSettingsIO::safeReadFile(SETTINGS_FILE_JSON);
  if (!json.isEmpty()) {
    bool resave = false;
    bool result = JsonSettingsIO::loadSettings(*this, json.c_str(), &resave);
    if (result && resave) {
      if (saveToFile()) {
        LOG_DBG("CPS", "Resaved settings to update format");
      } else {
        LOG_ERR("CPS", "Failed to resave settings after format update");
      }
    }
    return result;
  }

  // Fall back to binary migration
  if (Storage.exists(SETTINGS_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      if (saveToFile()) {
        Storage.rename(SETTINGS_FILE_BIN, SETTINGS_FILE_BAK);
        LOG_DBG("CPS", "Migrated settings.bin to settings.json");
        return true;
      } else {
        LOG_ERR("CPS", "Failed to save migrated settings to JSON");
        return false;
      }
    }
  }

  // Fallback to old backed up settings if .json was destroyed in a crash
  if (Storage.exists(SETTINGS_FILE_BAK)) {
    // temporarily swap name and load it as binary
    Storage.rename(SETTINGS_FILE_BAK, SETTINGS_FILE_BIN);
    bool didLoad = loadFromBinaryFile();
    if (didLoad && saveToFile()) {
      Storage.rename(SETTINGS_FILE_BIN, SETTINGS_FILE_BAK);
      LOG_DBG("CPS", "Recovered settings from settings.bin.bak");
      return true;
      Storage.rename(SETTINGS_FILE_BIN, SETTINGS_FILE_BAK);
      LOG_DBG("CPS", "Recovered settings from settings.bin.bak");
    }
  }

  return false;
}

bool CrossPointSettings::loadFromBinaryFile() {
  FsFile inputFile;
  if (!Storage.openFileForRead("CPS", SETTINGS_FILE_BIN, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version != SETTINGS_FILE_VERSION) {
    inputFile.close();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);
  logSerial.printf("[CPS] loadFromFile: version=%d, count=%d\n", (int)version,
                   (int)fileSettingsCount);

  uint8_t settingsRead = 0;
  bool frontButtonMappingRead = false;
  bool splitReaderMarginsRead = false;
  bool statusBarGranularRead = false;
  do {
    readAndValidate(inputFile, sleepScreen, SLEEP_SCREEN_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, extraParagraphSpacing);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, shortPwrBtn, SHORT_PWRBTN_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, statusBar, STATUS_BAR_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, orientation, ORIENTATION_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, frontButtonLayout,
                    FRONT_BUTTON_LAYOUT_COUNT); // legacy
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, sideButtonLayout, SIDE_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    {
      uint8_t storedFontFamily = BOOKERLY;
      serialization::readPod(inputFile, storedFontFamily);
      // Current format: 0 = Charter, 1 = EB Garamond.
      if (storedFontFamily < FONT_FAMILY_COUNT) {
        fontFamily = storedFontFamily;
      } else {
        fontFamily = BOOKERLY;
      }
    }
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, fontSize, FONT_SIZE_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, lineSpacing, LINE_COMPRESSION_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, paragraphAlignment, PARAGRAPH_ALIGNMENT_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, sleepTimeout, SLEEP_TIMEOUT_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, refreshFrequency, REFRESH_FREQUENCY_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, screenMargin);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, sleepScreenCoverMode,
                    SLEEP_SCREEN_COVER_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    {
      std::string urlStr;
      serialization::readString(inputFile, urlStr);
      strncpy(opdsServerUrl, urlStr.c_str(), sizeof(opdsServerUrl) - 1);
      opdsServerUrl[sizeof(opdsServerUrl) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, textAntiAliasing);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, hideBatteryPercentage,
                    HIDE_BATTERY_PERCENTAGE_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, longPressChapterSkip);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, hyphenationEnabled);
    if (++settingsRead >= fileSettingsCount)
      break;
    {
      std::string usernameStr;
      serialization::readString(inputFile, usernameStr);
      strncpy(opdsUsername, usernameStr.c_str(), sizeof(opdsUsername) - 1);
      opdsUsername[sizeof(opdsUsername) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount)
      break;
    {
      std::string passwordStr;
      serialization::readString(inputFile, passwordStr);
      strncpy(opdsPassword, passwordStr.c_str(), sizeof(opdsPassword) - 1);
      opdsPassword[sizeof(opdsPassword) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, sleepScreenCoverFilter,
                    SLEEP_SCREEN_COVER_FILTER_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, frontButtonBack, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, frontButtonConfirm, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, frontButtonLeft, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, frontButtonRight, FRONT_BUTTON_HARDWARE_COUNT);
    frontButtonMappingRead = true;
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, fadingFix);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, embeddedStyle);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, screenMarginHorizontal);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, screenMarginTop);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, screenMarginBottom);
    splitReaderMarginsRead = true;
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, showSleepImageFilename);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, statusBarEnabled);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, statusBarShowBattery);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, statusBarShowPageCounter);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, statusBarShowBookPercentage);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, statusBarShowChapterPercentage);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, statusBarShowBookBar);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, statusBarShowChapterBar);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, statusBarShowChapterTitle);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, statusBarTopLine);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, statusBarTextAlignment,
                    STATUS_TEXT_ALIGNMENT_COUNT);
    if (++settingsRead >= fileSettingsCount)
      break;
    readAndValidate(inputFile, statusBarProgressStyle,
                    STATUS_BAR_PROGRESS_STYLE_COUNT);
    statusBarGranularRead = true;
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, readerBoldSwap);
    if (++settingsRead >= fileSettingsCount)
      break;
    serialization::readPod(inputFile, debugBorders);
    if (++settingsRead >= fileSettingsCount)
      break;
    // New fields added at end for backward compatibility
  } while (false);

  if (frontButtonMappingRead) {
    CrossPointSettings::validateFrontButtonMapping(*this);
  } else {
    applyLegacyFrontButtonLayout(*this);
  }

  // Migration path for older settings files that only had uniform screenMargin.
  if (!splitReaderMarginsRead) {
    screenMarginHorizontal = screenMargin;
    screenMarginTop = screenMargin;
    screenMarginBottom = screenMargin;
  }

  if (!statusBarGranularRead) {
    migrateLegacyStatusBarMode(*this);
  }

  // Binary settings only store legacy 3-step spacing; map it to percent.
  lineSpacingPercent = legacyLineSpacingToPercent(lineSpacing);

  inputFile.close();
  LOG_DBG("CPS", "Settings loaded from binary file");
  return true;
}

float CrossPointSettings::getReaderLineCompression() const {
  uint8_t spacing = lineSpacingPercent;
  if (spacing < 65) {
    spacing = 65;
  } else if (spacing > 150) {
    spacing = 150;
  }
  return static_cast<float>(spacing) / 100.0f;
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  switch (sleepTimeout) {
  case SLEEP_1_MIN:
    return 1UL * 60 * 1000;
  case SLEEP_5_MIN:
    return 5UL * 60 * 1000;
  case SLEEP_10_MIN:
  default:
    return 10UL * 60 * 1000;
  case SLEEP_15_MIN:
    return 15UL * 60 * 1000;
  case SLEEP_30_MIN:
    return 30UL * 60 * 1000;
  }
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
  case REFRESH_1:
    return 1;
  case REFRESH_5:
    return 5;
  case REFRESH_10:
    return 10;
  case REFRESH_15:
  default:
    return 15;
  case REFRESH_30:
    return 30;
  }
}

int CrossPointSettings::getReaderFontId() const {
  switch (fontFamily) {
  case CHAREINK:
    switch (fontSize) {
    case LARGE:
      return CHAREINK_18_FONT_ID;
    case MEDIUM:
    default:
      return CHAREINK_16_FONT_ID;
    }
  case ATKINSON:
    switch (fontSize) {
    case LARGE:
      return ATKINSON_18_FONT_ID;
    case MEDIUM:
    default:
      return ATKINSON_16_FONT_ID;
    }
  case UBUNTU:
    switch (fontSize) {
    case LARGE:
      return UBUNTU_18_FONT_ID;
    case MEDIUM:
    default:
      return UBUNTU_16_FONT_ID;
    }
  case BOOKERLY:
  default:
    switch (fontSize) {
    case LARGE:
      return BOOKERLY_18_FONT_ID;
    case MEDIUM:
    default:
      return BOOKERLY_16_FONT_ID;
    }
  }
}

int CrossPointSettings::getStatusBarProgressBarHeight() const { return 6; }
