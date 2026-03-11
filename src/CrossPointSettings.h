#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <iosfwd>

class CrossPointSettings {
private:
  // Private constructor for singleton
  CrossPointSettings() = default;

  // Static instance
  static CrossPointSettings instance;

public:
  // Delete copy constructor and assignment
  CrossPointSettings(const CrossPointSettings &) = delete;
  CrossPointSettings &operator=(const CrossPointSettings &) = delete;

  enum SLEEP_SCREEN_MODE {
    DARK = 0,
    LIGHT = 1,
    CUSTOM = 2,
    COVER = 3,
    BLANK = 4,
    COVER_CUSTOM = 5,
    SLEEP_SCREEN_MODE_COUNT
  };
  enum SLEEP_SCREEN_COVER_MODE {
    FIT = 0,
    CROP = 1,
    SLEEP_SCREEN_COVER_MODE_COUNT
  };
  enum SLEEP_SCREEN_COVER_FILTER {
    NO_FILTER = 0,
    BLACK_AND_WHITE = 1,
    INVERTED_BLACK_AND_WHITE = 2,
    SLEEP_SCREEN_COVER_FILTER_COUNT
  };

  // Status bar display type enum
  enum STATUS_BAR_MODE {
    NONE = 0,
    NO_PROGRESS = 1,
    FULL = 2,
    BOOK_PROGRESS_BAR = 3,
    ONLY_BOOK_PROGRESS_BAR = 4,
    CHAPTER_PROGRESS_BAR = 5,
    STATUS_BAR_MODE_COUNT
  };
  enum STATUS_BAR_TEXT_ALIGNMENT {
    STATUS_TEXT_RIGHT = 0,
    STATUS_TEXT_CENTER = 1,
    STATUS_TEXT_LEFT = 2,
    STATUS_TEXT_ALIGNMENT_COUNT
  };
  enum STATUS_BAR_PROGRESS_STYLE {
    STATUS_BAR_THIN = 0,
    STATUS_BAR_THICK = 1,
    STATUS_BAR_DOTTED = 2,
    STATUS_BAR_PROGRESS_STYLE_COUNT
  };

  enum ORIENTATION {
    PORTRAIT = 0, // 480x800 logical coordinates (current default)
    LANDSCAPE_CW =
        1,        // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    INVERTED = 2, // 480x800 logical coordinates, inverted
    LANDSCAPE_CCW = 3, // 800x480 logical coordinates, native panel orientation
    ORIENTATION_COUNT
  };

  // Front button layout options (legacy)
  // Default: Back, Confirm, Left, Right
  // Swapped: Left, Right, Back, Confirm
  enum FRONT_BUTTON_LAYOUT {
    BACK_CONFIRM_LEFT_RIGHT = 0,
    LEFT_RIGHT_BACK_CONFIRM = 1,
    LEFT_BACK_CONFIRM_RIGHT = 2,
    BACK_CONFIRM_RIGHT_LEFT = 3,
    FRONT_BUTTON_LAYOUT_COUNT
  };

  // Front button hardware identifiers (for remapping)
  enum FRONT_BUTTON_HARDWARE {
    FRONT_HW_BACK = 0,
    FRONT_HW_CONFIRM = 1,
    FRONT_HW_LEFT = 2,
    FRONT_HW_RIGHT = 3,
    FRONT_BUTTON_HARDWARE_COUNT
  };

  // Side button layout options
  // Default: Previous, Next
  // Swapped: Next, Previous
  enum SIDE_BUTTON_LAYOUT {
    PREV_NEXT = 0,
    NEXT_PREV = 1,
    SIDE_BUTTON_LAYOUT_COUNT
  };

  // Font family options
  enum FONT_FAMILY {
    CHAREINK = 0,
    LEGACY_REMOVED_FAMILY = 1,
    FREESERIF = 2,
    LEGACY_REMOVED_FAMILY_2 = 3,
    FONT_FAMILY_COUNT
  };
  // Reader font sizes are family-dependent:
  // ChareInk uses 13, 14, 15, 16, 17, 18, and 19.
  // FreeSerif uses 19, 21, and 23.
  // The FreeSerif family reuses the former Bookerly enum value so older saved
  // settings migrate into the new serif family automatically.
  enum FONT_SIZE {
    MEDIUM = 0,   // legacy 15 -> normalize to 16
    LARGE = 1,    // legacy 17
    X_LARGE = 2,  // legacy 19
    SIZE_14 = 3,  // legacy 14 -> normalize to 16
    SIZE_16 = 4,
    SIZE_18 = 5,
    SIZE_13 = 6,  // legacy 13 -> normalize to 16
    SIZE_12 = 7,
    FONT_SIZE_COUNT
  };
  // Legacy line spacing enum (kept for settings migration compatibility)
  enum LINE_COMPRESSION {
    TIGHT = 0,
    NORMAL = 1,
    WIDE = 2,
    LINE_COMPRESSION_COUNT
  };
  enum PARAGRAPH_ALIGNMENT {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    BOOK_STYLE = 4,
    PARAGRAPH_ALIGNMENT_COUNT
  };
  enum EXTRA_PARAGRAPH_SPACING_LEVEL {
    EXTRA_SPACING_OFF = 0,
    EXTRA_SPACING_S = 1,
    EXTRA_SPACING_M = 2,
    EXTRA_SPACING_L = 3,
    EXTRA_PARAGRAPH_SPACING_COUNT
  };

  // Auto-sleep timeout options (in minutes)
  enum SLEEP_TIMEOUT {
    SLEEP_1_MIN = 0,
    SLEEP_5_MIN = 1,
    SLEEP_10_MIN = 2,
    SLEEP_15_MIN = 3,
    SLEEP_30_MIN = 4,
    SLEEP_TIMEOUT_COUNT
  };

  // E-ink refresh frequency (pages between full refreshes)
  enum REFRESH_FREQUENCY {
    REFRESH_1 = 0,
    REFRESH_5 = 1,
    REFRESH_10 = 2,
    REFRESH_15 = 3,
    REFRESH_30 = 4,
    REFRESH_FREQUENCY_COUNT
  };

  // Short power button press actions
  enum SHORT_PWRBTN {
    IGNORE = 0,
    SLEEP = 1,
    PAGE_TURN = 2,
    SHORT_PWRBTN_COUNT
  };

  // Hide battery percentage
  enum HIDE_BATTERY_PERCENTAGE {
    HIDE_NEVER = 0,
    HIDE_READER = 1,
    HIDE_ALWAYS = 2,
    HIDE_BATTERY_PERCENTAGE_COUNT
  };

  // Sleep screen settings
  uint8_t sleepScreen = DARK;
  // Sleep screen cover mode settings
  uint8_t sleepScreenCoverMode = FIT;
  // Sleep screen cover filter
  uint8_t sleepScreenCoverFilter = NO_FILTER;
  // Show custom sleep image filename label
  uint8_t showSleepImageFilename = 0;
  // Status bar settings
  uint8_t statusBar = FULL;
  uint8_t statusBarEnabled = 1;
  uint8_t statusBarShowBattery = 1;
  uint8_t statusBarShowPageCounter = 0;
  uint8_t statusBarShowBookPercentage = 0;
  uint8_t statusBarShowChapterPercentage = 0;
  uint8_t statusBarShowBookBar = 0;
  uint8_t statusBarShowChapterBar = 0;
  uint8_t statusBarShowChapterTitle = 1;
  uint8_t statusBarNoTitleTruncation = 0;
  uint8_t statusBarTopLine = 0;
  uint8_t statusBarTextAlignment = STATUS_TEXT_RIGHT;
  uint8_t statusBarProgressStyle = STATUS_BAR_THICK;
  // Text rendering settings
  uint8_t extraParagraphSpacingLevel = EXTRA_SPACING_M;
  uint8_t textAntiAliasing = 1;
  // Short power button click behaviour
  uint8_t shortPwrBtn = IGNORE;
  // EPUB reading orientation settings
  // 0 = portrait (default), 1 = landscape clockwise, 2 = inverted, 3 =
  // landscape counter-clockwise
  uint8_t orientation = PORTRAIT;
  // Button layouts (front layout retained for migration only)
  uint8_t frontButtonLayout = BACK_CONFIRM_LEFT_RIGHT;
  uint8_t sideButtonLayout = PREV_NEXT;
  // Front button remap (logical -> hardware)
  // Used by MappedInputManager to translate logical buttons into physical front
  // buttons.
  uint8_t frontButtonBack = FRONT_HW_BACK;
  uint8_t frontButtonConfirm = FRONT_HW_CONFIRM;
  uint8_t frontButtonLeft = FRONT_HW_LEFT;
  uint8_t frontButtonRight = FRONT_HW_RIGHT;
  // Reader font settings
  uint8_t fontFamily = CHAREINK;
  uint8_t fontSize = SIZE_16;
  // Legacy line spacing setting (kept for migration from old settings files)
  uint8_t lineSpacing = NORMAL;
  // Reader line spacing percentage (65..150)
  uint8_t lineSpacingPercent = 110;
  uint8_t paragraphAlignment = JUSTIFIED;
  // Reader-only style swap: Regular <-> Bold (italics unchanged)
  uint8_t readerBoldSwap = 0;
  // Auto-sleep timeout setting (default 10 minutes)
  uint8_t sleepTimeout = SLEEP_10_MIN;
  // E-ink refresh frequency (default 15 pages)
  uint8_t refreshFrequency = REFRESH_15;
  uint8_t hyphenationEnabled = 0;

  // Legacy uniform reader margin (kept for backward compatibility in settings
  // migration)
  uint8_t screenMargin = 20;
  // Reader screen margin settings
  uint8_t screenMarginHorizontal = 20;
  uint8_t screenMarginTop = 20;
  uint8_t screenMarginBottom = 20;
  // OPDS browser settings
  char opdsServerUrl[128] = "";
  char opdsUsername[64] = "";
  char opdsPassword[64] = "";
  // Hide battery percentage
  uint8_t hideBatteryPercentage = HIDE_NEVER;
  // Long-press chapter skip on side buttons
  uint8_t longPressChapterSkip = 1;
  // Sunlight fading compensation
  uint8_t fadingFix = 0;
  // Use book's embedded CSS styles for EPUB rendering (1 = enabled, 0 =
  // disabled)
  uint8_t embeddedStyle = 1;
  // Draw dotted debug borders around reader and status bar viewports
  uint8_t debugBorders = 0;

  ~CrossPointSettings() = default;

  // Get singleton instance
  static CrossPointSettings &getInstance() { return instance; }

  uint16_t getPowerButtonDuration() const {
    return (shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) ? 10 : 400;
  }
  static uint8_t normalizeFontFamily(uint8_t family);
  static uint8_t fontFamilyToDisplayIndex(uint8_t family);
  static uint8_t displayIndexToFontFamily(uint8_t displayIndex);
  static bool isSingleSizeFontFamily(uint8_t family);
  static uint8_t normalizeFontSizeForFamily(uint8_t family, uint8_t fontSize);
  static uint8_t defaultLineSpacingPercentForFamily(uint8_t family,
                                                    uint8_t currentPercent);
  static uint8_t nextFontSize(uint8_t family, uint8_t fontSize);
  static uint8_t fontSizeToPointSize(uint8_t family, uint8_t fontSize);
  static uint8_t fontSizeOptionCount(uint8_t family);
  static uint8_t fontSizeToDisplayIndex(uint8_t family, uint8_t fontSize);
  static uint8_t displayIndexToFontSize(uint8_t family, uint8_t displayIndex);
  int getReaderFontId() const;
  int getStatusBarProgressBarHeight() const;

  bool saveToFile() const;
  bool loadFromFile();

  static void validateFrontButtonMapping(CrossPointSettings &settings);

private:
  bool loadFromBinaryFile();

public:
  float getReaderLineCompression() const;
  unsigned long getSleepTimeoutMs() const;
  int getRefreshFrequency() const;
};

// Helper macro to access settings
#define SETTINGS CrossPointSettings::getInstance()
