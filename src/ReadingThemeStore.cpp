#include "ReadingThemeStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>

namespace {
constexpr char READING_THEMES_FILE_JSON[] = "/.crosspoint/reading_themes.json";
constexpr char BOOK_READER_SETTINGS_FILE_JSON[] = "/reader_settings.json";

uint8_t clampRange(const uint8_t value, const uint8_t minValue,
                   const uint8_t maxValue, const uint8_t defaultValue) {
  if (value < minValue || value > maxValue) {
    return defaultValue;
  }
  return value;
}

bool sameThemeName(const std::string& left, const std::string& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (size_t i = 0; i < left.size(); i++) {
    const char a = static_cast<char>(
        std::tolower(static_cast<unsigned char>(left[i])));
    const char b = static_cast<char>(
        std::tolower(static_cast<unsigned char>(right[i])));
    if (a != b) {
      return false;
    }
  }
  return true;
}

std::string bookReaderSettingsPath(const std::string& cachePath) {
  return cachePath + BOOK_READER_SETTINGS_FILE_JSON;
}
}  // namespace

ReadingThemeStore ReadingThemeStore::instance;

const ReadingTheme* ReadingThemeStore::getTheme(const size_t index) const {
  if (index >= themes.size()) {
    return nullptr;
  }
  return &themes[index];
}

bool ReadingThemeStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveReadingThemes(*this, READING_THEMES_FILE_JSON);
}

bool ReadingThemeStore::loadFromFile() {
  const String json = JsonSettingsIO::safeReadFile(READING_THEMES_FILE_JSON);
  if (json.isEmpty()) {
    return false;
  }
  return JsonSettingsIO::loadReadingThemes(*this, json.c_str());
}

ReadingTheme ReadingThemeStore::fromSettings(const std::string& name,
                                             const CrossPointSettings& settings) {
  ReadingTheme theme;
  theme.name = sanitizeName(name);
  theme.fontFamily = settings.fontFamily;
  theme.fontSize = settings.fontSize;
  theme.lineSpacingPercent = settings.lineSpacingPercent;
  theme.screenMarginHorizontal = settings.screenMarginHorizontal;
  theme.screenMarginTop = settings.screenMarginTop;
  theme.screenMarginBottom = settings.screenMarginBottom;
  theme.paragraphAlignment = settings.paragraphAlignment;
  theme.extraParagraphSpacingLevel = settings.extraParagraphSpacingLevel;
  theme.embeddedStyle = settings.embeddedStyle;
  theme.hyphenationEnabled = settings.hyphenationEnabled;
  theme.statusBarEnabled = settings.statusBarEnabled;
  theme.statusBarShowBattery = settings.statusBarShowBattery;
  theme.statusBarShowPageCounter = settings.statusBarShowPageCounter;
  theme.statusBarPageCounterMode = settings.statusBarPageCounterMode;
  theme.statusBarShowBookPercentage = settings.statusBarShowBookPercentage;
  theme.statusBarShowChapterPercentage = settings.statusBarShowChapterPercentage;
  theme.statusBarShowBookBar = settings.statusBarShowBookBar;
  theme.statusBarShowChapterBar = settings.statusBarShowChapterBar;
  theme.statusBarShowChapterTitle = settings.statusBarShowChapterTitle;
  theme.statusBarNoTitleTruncation = settings.statusBarNoTitleTruncation;
  theme.statusBarBatteryPosition = settings.statusBarBatteryPosition;
  theme.statusBarProgressTextPosition = settings.statusBarProgressTextPosition;
  theme.statusBarPageCounterPosition = settings.statusBarPageCounterPosition;
  theme.statusBarBookPercentagePosition =
      settings.statusBarBookPercentagePosition;
  theme.statusBarChapterPercentagePosition =
      settings.statusBarChapterPercentagePosition;
  theme.statusBarBookBarPosition = settings.statusBarBookBarPosition;
  theme.statusBarChapterBarPosition = settings.statusBarChapterBarPosition;
  theme.statusBarTitlePosition = settings.statusBarTitlePosition;
  theme.statusBarTextAlignment = settings.statusBarTextAlignment;
  theme.statusBarProgressStyle = settings.statusBarProgressStyle;
  return theme;
}

ReadingTheme ReadingThemeStore::captureCurrent(const std::string& name) const {
  return fromSettings(name, SETTINGS);
}

void ReadingThemeStore::applyThemeToSettings(const ReadingTheme& theme,
                                             CrossPointSettings& settings) {
  settings.fontFamily =
      CrossPointSettings::normalizeFontFamily(theme.fontFamily);
  settings.fontSize = CrossPointSettings::normalizeFontSizeForFamily(
      settings.fontFamily, theme.fontSize);
  settings.lineSpacingPercent =
      clampRange(theme.lineSpacingPercent, 65, 150, 110);
  settings.screenMarginHorizontal =
      clampRange(theme.screenMarginHorizontal, 0, 55, 20);
  settings.screenMarginTop = clampRange(theme.screenMarginTop, 0, 55, 20);
  settings.screenMarginBottom =
      clampRange(theme.screenMarginBottom, 0, 55, 20);
  settings.paragraphAlignment = clampRange(
      theme.paragraphAlignment, 0,
      CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT - 1,
      CrossPointSettings::JUSTIFIED);
  settings.extraParagraphSpacingLevel = clampRange(
      theme.extraParagraphSpacingLevel, 0,
      CrossPointSettings::EXTRA_PARAGRAPH_SPACING_COUNT - 1,
      CrossPointSettings::EXTRA_SPACING_M);
  settings.embeddedStyle = theme.embeddedStyle ? 1 : 0;
  settings.hyphenationEnabled = theme.hyphenationEnabled ? 1 : 0;
  settings.statusBarEnabled = theme.statusBarEnabled ? 1 : 0;
  settings.statusBarShowBattery = theme.statusBarShowBattery ? 1 : 0;
  settings.statusBarShowPageCounter = theme.statusBarShowPageCounter ? 1 : 0;
  settings.statusBarPageCounterMode = clampRange(
      theme.statusBarPageCounterMode, 0,
      CrossPointSettings::STATUS_BAR_PAGE_COUNTER_MODE_COUNT - 1,
      CrossPointSettings::STATUS_PAGE_CURRENT_TOTAL);
  settings.statusBarShowBookPercentage =
      theme.statusBarShowBookPercentage ? 1 : 0;
  settings.statusBarShowChapterPercentage =
      theme.statusBarShowChapterPercentage ? 1 : 0;
  settings.statusBarShowBookBar = theme.statusBarShowBookBar ? 1 : 0;
  settings.statusBarShowChapterBar = theme.statusBarShowChapterBar ? 1 : 0;
  settings.statusBarShowChapterTitle = theme.statusBarShowChapterTitle ? 1 : 0;
  settings.statusBarNoTitleTruncation =
      theme.statusBarNoTitleTruncation ? 1 : 0;
  settings.statusBarBatteryPosition = clampRange(
      theme.statusBarBatteryPosition, 0,
      CrossPointSettings::STATUS_BAR_ITEM_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_AT_BOTTOM);
  settings.statusBarProgressTextPosition = clampRange(
      theme.statusBarProgressTextPosition, 0,
      CrossPointSettings::STATUS_BAR_ITEM_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_AT_BOTTOM);
  settings.statusBarPageCounterPosition = clampRange(
      theme.statusBarPageCounterPosition, 0,
      CrossPointSettings::STATUS_BAR_TEXT_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_TEXT_BOTTOM_CENTER);
  settings.statusBarBookPercentagePosition = clampRange(
      theme.statusBarBookPercentagePosition, 0,
      CrossPointSettings::STATUS_BAR_TEXT_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_TEXT_BOTTOM_CENTER);
  settings.statusBarChapterPercentagePosition = clampRange(
      theme.statusBarChapterPercentagePosition, 0,
      CrossPointSettings::STATUS_BAR_TEXT_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_TEXT_BOTTOM_CENTER);
  settings.statusBarBookBarPosition = clampRange(
      theme.statusBarBookBarPosition, 0,
      CrossPointSettings::STATUS_BAR_ITEM_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_AT_BOTTOM);
  settings.statusBarChapterBarPosition = clampRange(
      theme.statusBarChapterBarPosition, 0,
      CrossPointSettings::STATUS_BAR_ITEM_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_AT_BOTTOM);
  settings.statusBarTitlePosition = clampRange(
      theme.statusBarTitlePosition, 0,
      CrossPointSettings::STATUS_BAR_ITEM_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_AT_BOTTOM);
  settings.statusBarTextAlignment = clampRange(
      theme.statusBarTextAlignment, 0,
      CrossPointSettings::STATUS_TEXT_ALIGNMENT_COUNT - 1,
      CrossPointSettings::STATUS_TEXT_RIGHT);
  settings.statusBarProgressStyle = clampRange(
      theme.statusBarProgressStyle, 0,
      CrossPointSettings::STATUS_BAR_PROGRESS_STYLE_COUNT - 1,
      CrossPointSettings::STATUS_BAR_THICK);
}

bool ReadingThemeStore::matchesCurrent(const ReadingTheme& theme) const {
  ReadingTheme current = fromSettings("", SETTINGS);
  return current.fontFamily == theme.fontFamily &&
         current.fontSize == theme.fontSize &&
         current.lineSpacingPercent == theme.lineSpacingPercent &&
         current.screenMarginHorizontal == theme.screenMarginHorizontal &&
         current.screenMarginTop == theme.screenMarginTop &&
         current.screenMarginBottom == theme.screenMarginBottom &&
         current.paragraphAlignment == theme.paragraphAlignment &&
         current.extraParagraphSpacingLevel ==
             theme.extraParagraphSpacingLevel &&
         current.embeddedStyle == theme.embeddedStyle &&
         current.hyphenationEnabled == theme.hyphenationEnabled &&
         current.statusBarEnabled == theme.statusBarEnabled &&
         current.statusBarShowBattery == theme.statusBarShowBattery &&
         current.statusBarShowPageCounter == theme.statusBarShowPageCounter &&
         current.statusBarPageCounterMode == theme.statusBarPageCounterMode &&
         current.statusBarShowBookPercentage ==
             theme.statusBarShowBookPercentage &&
         current.statusBarShowChapterPercentage ==
             theme.statusBarShowChapterPercentage &&
         current.statusBarShowBookBar == theme.statusBarShowBookBar &&
         current.statusBarShowChapterBar == theme.statusBarShowChapterBar &&
         current.statusBarShowChapterTitle ==
             theme.statusBarShowChapterTitle &&
         current.statusBarNoTitleTruncation ==
             theme.statusBarNoTitleTruncation &&
         current.statusBarBatteryPosition ==
             theme.statusBarBatteryPosition &&
         current.statusBarProgressTextPosition ==
             theme.statusBarProgressTextPosition &&
         current.statusBarPageCounterPosition ==
             theme.statusBarPageCounterPosition &&
         current.statusBarBookPercentagePosition ==
             theme.statusBarBookPercentagePosition &&
         current.statusBarChapterPercentagePosition ==
             theme.statusBarChapterPercentagePosition &&
         current.statusBarBookBarPosition == theme.statusBarBookBarPosition &&
         current.statusBarChapterBarPosition ==
             theme.statusBarChapterBarPosition &&
         current.statusBarTitlePosition == theme.statusBarTitlePosition &&
         current.statusBarTextAlignment == theme.statusBarTextAlignment &&
         current.statusBarProgressStyle == theme.statusBarProgressStyle;
}

int ReadingThemeStore::findMatchingTheme() const {
  for (size_t i = 0; i < themes.size(); i++) {
    if (matchesCurrent(themes[i])) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

std::string ReadingThemeStore::sanitizeName(const std::string& name) {
  std::string trimmed;
  trimmed.reserve(name.size());
  bool seenNonSpace = false;
  for (char c : name) {
    const bool isSpace = std::isspace(static_cast<unsigned char>(c)) != 0;
    if (!seenNonSpace && isSpace) {
      continue;
    }
    seenNonSpace = true;
    trimmed.push_back(c);
  }
  while (!trimmed.empty() &&
         std::isspace(static_cast<unsigned char>(trimmed.back())) != 0) {
    trimmed.pop_back();
  }
  if (trimmed.empty()) {
    trimmed = "Theme";
  }
  if (trimmed.size() > MAX_THEME_NAME_LENGTH) {
    trimmed.resize(MAX_THEME_NAME_LENGTH);
  }
  return trimmed;
}

std::string ReadingThemeStore::makeUniqueName(const std::string& desiredName,
                                              const int ignoreIndex) const {
  const std::string base = sanitizeName(desiredName);
  std::string candidate = base;
  int suffix = 2;
  auto conflicts = [&](const std::string& name) {
    for (size_t i = 0; i < themes.size(); i++) {
      if (static_cast<int>(i) == ignoreIndex) {
        continue;
      }
      if (sameThemeName(themes[i].name, name)) {
        return true;
      }
    }
    return false;
  };

  while (conflicts(candidate)) {
    const std::string suffixText = " " + std::to_string(suffix);
    candidate = base;
    if (candidate.size() + suffixText.size() > MAX_THEME_NAME_LENGTH) {
      candidate.resize(MAX_THEME_NAME_LENGTH - suffixText.size());
    }
    candidate += suffixText;
    suffix++;
  }
  return candidate;
}

bool ReadingThemeStore::addTheme(const std::string& name) {
  if (themes.size() >= MAX_THEMES) {
    return false;
  }
  themes.push_back(captureCurrent(makeUniqueName(name)));
  return saveToFile();
}

bool ReadingThemeStore::updateTheme(const size_t index) {
  if (index >= themes.size()) {
    return false;
  }
  themes[index] = captureCurrent(themes[index].name);
  return saveToFile();
}

bool ReadingThemeStore::renameTheme(const size_t index,
                                    const std::string& desiredName) {
  if (index >= themes.size()) {
    return false;
  }
  themes[index].name = makeUniqueName(desiredName, static_cast<int>(index));
  return saveToFile();
}

bool ReadingThemeStore::deleteTheme(const size_t index) {
  if (index >= themes.size()) {
    return false;
  }
  themes.erase(themes.begin() + static_cast<long>(index));
  return saveToFile();
}

bool ReadingThemeStore::applyTheme(const size_t index) {
  if (index >= themes.size()) {
    return false;
  }
  applyThemeToSettings(themes[index], SETTINGS);
  return SETTINGS.saveToFile();
}

bool ReadingThemeStore::saveCurrentBookSettings(const std::string& cachePath) {
  if (cachePath.empty()) {
    return false;
  }
  return JsonSettingsIO::saveReadingTheme(
      fromSettings("", SETTINGS), bookReaderSettingsPath(cachePath).c_str());
}

bool ReadingThemeStore::loadBookSettingsIntoCurrent(
    const std::string& cachePath) {
  if (cachePath.empty()) {
    return false;
  }

  const String json =
      JsonSettingsIO::safeReadFile(bookReaderSettingsPath(cachePath).c_str());
  if (json.isEmpty()) {
    return false;
  }

  ReadingTheme theme;
  if (!JsonSettingsIO::loadReadingTheme(theme, json.c_str())) {
    return false;
  }

  applyThemeToSettings(theme, SETTINGS);
  return true;
}

ReadingTheme ReadingThemeStore::normalizeTheme(const ReadingTheme& theme) {
  ReadingTheme normalized = theme;
  normalized.name = sanitizeName(theme.name);
  normalized.fontFamily =
      CrossPointSettings::normalizeFontFamily(theme.fontFamily);
  normalized.fontSize = CrossPointSettings::normalizeFontSizeForFamily(
      normalized.fontFamily, theme.fontSize);
  normalized.lineSpacingPercent =
      clampRange(theme.lineSpacingPercent, 65, 150, 110);
  normalized.screenMarginHorizontal =
      clampRange(theme.screenMarginHorizontal, 0, 55, 20);
  normalized.screenMarginTop = clampRange(theme.screenMarginTop, 0, 55, 20);
  normalized.screenMarginBottom =
      clampRange(theme.screenMarginBottom, 0, 55, 20);
  normalized.paragraphAlignment = clampRange(
      theme.paragraphAlignment, 0,
      CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT - 1,
      CrossPointSettings::JUSTIFIED);
  normalized.extraParagraphSpacingLevel = clampRange(
      theme.extraParagraphSpacingLevel, 0,
      CrossPointSettings::EXTRA_PARAGRAPH_SPACING_COUNT - 1,
      CrossPointSettings::EXTRA_SPACING_M);
  normalized.embeddedStyle = theme.embeddedStyle ? 1 : 0;
  normalized.hyphenationEnabled = theme.hyphenationEnabled ? 1 : 0;
  normalized.statusBarEnabled = theme.statusBarEnabled ? 1 : 0;
  normalized.statusBarShowBattery = theme.statusBarShowBattery ? 1 : 0;
  normalized.statusBarShowPageCounter = theme.statusBarShowPageCounter ? 1 : 0;
  normalized.statusBarPageCounterMode = clampRange(
      theme.statusBarPageCounterMode, 0,
      CrossPointSettings::STATUS_BAR_PAGE_COUNTER_MODE_COUNT - 1,
      CrossPointSettings::STATUS_PAGE_CURRENT_TOTAL);
  normalized.statusBarShowBookPercentage =
      theme.statusBarShowBookPercentage ? 1 : 0;
  normalized.statusBarShowChapterPercentage =
      theme.statusBarShowChapterPercentage ? 1 : 0;
  normalized.statusBarShowBookBar = theme.statusBarShowBookBar ? 1 : 0;
  normalized.statusBarShowChapterBar = theme.statusBarShowChapterBar ? 1 : 0;
  normalized.statusBarShowChapterTitle =
      theme.statusBarShowChapterTitle ? 1 : 0;
  normalized.statusBarNoTitleTruncation =
      theme.statusBarNoTitleTruncation ? 1 : 0;
  normalized.statusBarBatteryPosition = clampRange(
      theme.statusBarBatteryPosition, 0,
      CrossPointSettings::STATUS_BAR_ITEM_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_AT_BOTTOM);
  normalized.statusBarProgressTextPosition = clampRange(
      theme.statusBarProgressTextPosition, 0,
      CrossPointSettings::STATUS_BAR_ITEM_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_AT_BOTTOM);
  normalized.statusBarPageCounterPosition = clampRange(
      theme.statusBarPageCounterPosition, 0,
      CrossPointSettings::STATUS_BAR_TEXT_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_TEXT_BOTTOM_CENTER);
  normalized.statusBarBookPercentagePosition = clampRange(
      theme.statusBarBookPercentagePosition, 0,
      CrossPointSettings::STATUS_BAR_TEXT_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_TEXT_BOTTOM_CENTER);
  normalized.statusBarChapterPercentagePosition = clampRange(
      theme.statusBarChapterPercentagePosition, 0,
      CrossPointSettings::STATUS_BAR_TEXT_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_TEXT_BOTTOM_CENTER);
  normalized.statusBarBookBarPosition = clampRange(
      theme.statusBarBookBarPosition, 0,
      CrossPointSettings::STATUS_BAR_ITEM_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_AT_BOTTOM);
  normalized.statusBarChapterBarPosition = clampRange(
      theme.statusBarChapterBarPosition, 0,
      CrossPointSettings::STATUS_BAR_ITEM_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_AT_BOTTOM);
  normalized.statusBarTitlePosition = clampRange(
      theme.statusBarTitlePosition, 0,
      CrossPointSettings::STATUS_BAR_ITEM_POSITION_COUNT - 1,
      CrossPointSettings::STATUS_AT_BOTTOM);
  normalized.statusBarTextAlignment = clampRange(
      theme.statusBarTextAlignment, 0,
      CrossPointSettings::STATUS_TEXT_ALIGNMENT_COUNT - 1,
      CrossPointSettings::STATUS_TEXT_RIGHT);
  normalized.statusBarProgressStyle = clampRange(
      theme.statusBarProgressStyle, 0,
      CrossPointSettings::STATUS_BAR_PROGRESS_STYLE_COUNT - 1,
      CrossPointSettings::STATUS_BAR_THICK);
  return normalized;
}
