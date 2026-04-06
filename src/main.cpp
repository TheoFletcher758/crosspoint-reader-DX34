#include <Arduino.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <builtinFonts/all.h>

#include <cstring>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "ReadingThemeStore.h"
#include "RecentBooksStore.h"
#include "activities/boot_sleep/BootActivity.h"
#include "activities/boot_sleep/LastSleepWallpaperActivity.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/home/HomeActivity.h"
#include "activities/home/MyLibraryActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"
#include "util/TransitionFeedback.h"

HalDisplay display;
HalGPIO gpio;
MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
FontDecompressor fontDecompressor;
Activity* currentActivity;

// Fonts
EpdFont chareink14RegularFont(&chareink_14_regular);
EpdFont chareink14BoldFont(&chareink_14_bold);
EpdFont chareink14ItalicFont(&chareink_14_italic);
EpdFontFamily chareink14FontFamily(&chareink14RegularFont, &chareink14BoldFont, &chareink14ItalicFont, nullptr);
EpdFont chareink15RegularFont(&chareink_15_regular);
EpdFont chareink15BoldFont(&chareink_15_bold);
EpdFont chareink15ItalicFont(&chareink_15_italic);
EpdFontFamily chareink15FontFamily(&chareink15RegularFont, &chareink15BoldFont, &chareink15ItalicFont, nullptr);
EpdFont chareink16RegularFont(&chareink_16_regular);
EpdFont chareink16BoldFont(&chareink_16_bold);
EpdFont chareink16ItalicFont(&chareink_16_italic);
EpdFontFamily chareink16FontFamily(&chareink16RegularFont, &chareink16BoldFont, &chareink16ItalicFont, nullptr);
EpdFont chareink17RegularFont(&chareink_17_regular);
EpdFont chareink17BoldFont(&chareink_17_bold);
EpdFont chareink17ItalicFont(&chareink_17_italic);
EpdFontFamily chareink17FontFamily(&chareink17RegularFont, &chareink17BoldFont, &chareink17ItalicFont, nullptr);
EpdFont chareink18RegularFont(&chareink_18_regular);
EpdFont chareink18BoldFont(&chareink_18_bold);
EpdFont chareink18ItalicFont(&chareink_18_italic);
EpdFontFamily chareink18FontFamily(&chareink18RegularFont, &chareink18BoldFont, &chareink18ItalicFont, nullptr);
EpdFont bookerly13RegularFont(&bookerly_13_regular);
EpdFont bookerly13BoldFont(&bookerly_13_bold);
EpdFont bookerly13ItalicFont(&bookerly_13_italic);
EpdFontFamily bookerly13FontFamily(&bookerly13RegularFont, &bookerly13BoldFont, &bookerly13ItalicFont, nullptr);
EpdFont bookerly15RegularFont(&bookerly_15_regular);
EpdFont bookerly15BoldFont(&bookerly_15_bold);
EpdFont bookerly15ItalicFont(&bookerly_15_italic);
EpdFontFamily bookerly15FontFamily(&bookerly15RegularFont, &bookerly15BoldFont, &bookerly15ItalicFont, nullptr);
EpdFont bookerly18RegularFont(&bookerly_18_regular);
EpdFont bookerly18BoldFont(&bookerly_18_bold);
EpdFont bookerly18ItalicFont(&bookerly_18_italic);
EpdFontFamily bookerly18FontFamily(&bookerly18RegularFont, &bookerly18BoldFont, &bookerly18ItalicFont, nullptr);
EpdFont vollkorn15RegularFont(&vollkorn_15_regular);
EpdFont vollkorn15BoldFont(&vollkorn_15_bold);
EpdFont vollkorn15ItalicFont(&vollkorn_15_italic);
EpdFontFamily vollkorn15FontFamily(&vollkorn15RegularFont, &vollkorn15BoldFont, &vollkorn15ItalicFont, nullptr);
EpdFont vollkorn18RegularFont(&vollkorn_18_regular);
EpdFont vollkorn18BoldFont(&vollkorn_18_bold);
EpdFont vollkorn18ItalicFont(&vollkorn_18_italic);
EpdFontFamily vollkorn18FontFamily(&vollkorn18RegularFont, &vollkorn18BoldFont, &vollkorn18ItalicFont, nullptr);
EpdFont georgia15RegularFont(&georgia_15_regular);
EpdFont georgia15BoldFont(&georgia_15_bold);
EpdFont georgia15ItalicFont(&georgia_15_italic);
EpdFontFamily georgia15FontFamily(&georgia15RegularFont, &georgia15BoldFont, &georgia15ItalicFont, nullptr);
EpdFont georgia18RegularFont(&georgia_18_regular);
EpdFont georgia18BoldFont(&georgia_18_bold);
EpdFont georgia18ItalicFont(&georgia_18_italic);
EpdFontFamily georgia18FontFamily(&georgia18RegularFont, &georgia18BoldFont, &georgia18ItalicFont, nullptr);
EpdFont imfell15RegularFont(&imfell_15_regular);
EpdFont imfell15ItalicFont(&imfell_15_italic);
EpdFontFamily imfell15FontFamily(&imfell15RegularFont, nullptr, &imfell15ItalicFont, nullptr);
EpdFont imfell18RegularFont(&imfell_18_regular);
EpdFont imfell18ItalicFont(&imfell_18_italic);
EpdFontFamily imfell18FontFamily(&imfell18RegularFont, nullptr, &imfell18ItalicFont, nullptr);
EpdFont unifont14RegularFont(&unifont_14_regular);
EpdFontFamily unifont14FontFamily(&unifont14RegularFont, nullptr, nullptr, nullptr, 1, 0, false);
EpdFont unifont18RegularFont(&unifont_18_regular);
EpdFontFamily unifont18FontFamily(&unifont18RegularFont, nullptr, nullptr, nullptr, 1, 0, false);

EpdFont smallFont(&ui_8_regular);
EpdFontFamily smallFontFamily(&smallFont, nullptr, nullptr, nullptr, 0, 0, false);
EpdFont ui10RegularFont(&ui_10_regular);
EpdFontFamily ui10FontFamily(&ui10RegularFont, nullptr, nullptr, nullptr, 0, 0, false);
EpdFont ui12RegularFont(&ui_12_regular);
EpdFontFamily ui12FontFamily(&ui12RegularFont, nullptr, nullptr, nullptr, 0, 0, false);


// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

void exitActivity() {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }
}

void enterNewActivity(Activity* activity) {
  // Suppress stale button events from the previous activity so that
  // a press/release that closed the old screen doesn't leak into the new one.
  gpio.suppressUntilAllReleased();
  currentActivity = activity;
  currentActivity->onEnter();
}

void persistAppState(const char* context) {
  if (!APP_STATE.saveToFile()) {
    LOG_ERR("MAIN", "Failed to save app state (%s)", context);
  }
}

// Verify power button press duration on wake-up from deep sleep
// Pre-condition: isWakeupByPowerButton() == true
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    // Fast path for short press
    // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // Subtract the current time, because inputManager only starts counting the HeldTime from the first update()
  // This way, we remove the time we already took to reach here from the duration,
  // assuming the button was held until now from millis()==0 (i.e. device start time).
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < calibratedPressDuration);
    abort = gpio.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    powerManager.startDeepSleep(gpio);
  }
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  APP_STATE.lastSleepFromReader = currentActivity && currentActivity->isReaderActivity();
  persistAppState("enter deep sleep");
  exitActivity();
  enterNewActivity(new SleepActivity(renderer, mappedInputManager));

  display.deepSleep();
  LOG_DBG("MAIN", "Power button press calibration value: %lu ms", t2 - t1);
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

void onGoHome();
void onGoToMyLibraryWithPath(const std::string& path);
void onGoToRecentBooks();
void onGoToReader(const std::string& initialEpubPath) {
  const std::string bookPath = initialEpubPath;  // Copy before exitActivity() invalidates the reference
  TransitionFeedback::show(renderer, "Opening book...");
  exitActivity();
  enterNewActivity(new ReaderActivity(renderer, mappedInputManager, bookPath, onGoHome, onGoToMyLibraryWithPath));
}

void onGoToFileTransfer() {
  TransitionFeedback::show(renderer, "Starting server...");
  exitActivity();
  enterNewActivity(new CrossPointWebServerActivity(renderer, mappedInputManager, onGoHome));
}

void onGoToSettings() {
  TransitionFeedback::show(renderer, "Loading settings...");
  exitActivity();
  enterNewActivity(new SettingsActivity(renderer, mappedInputManager, onGoHome));
}

void onGoToMyLibrary() {
  TransitionFeedback::show(renderer, "Loading library...");
  persistAppState("go to library");
  exitActivity();
  enterNewActivity(new MyLibraryActivity(renderer, mappedInputManager, onGoHome, onGoToReader));
}

void onGoToRecentBooks() {
  TransitionFeedback::show(renderer, "Loading recents...");
  persistAppState("go to recents");
  exitActivity();
  enterNewActivity(new RecentBooksActivity(renderer, mappedInputManager, onGoHome, onGoToReader));
}

void onGoToMyLibraryWithPath(const std::string& path) {
  TransitionFeedback::show(renderer, "Loading library...");
  persistAppState("go to library path");
  exitActivity();
  enterNewActivity(new MyLibraryActivity(renderer, mappedInputManager, onGoHome, onGoToReader, path));
}

void onGoToBrowser() {
  TransitionFeedback::show(renderer, "Loading browser...");
  exitActivity();
  enterNewActivity(new OpdsBookBrowserActivity(renderer, mappedInputManager, onGoHome));
}

// Track whether home-screen data was loaded (deferred when booting to a book)
static bool homeDataLoaded = false;

void ensureHomeDataLoaded() {
  if (!homeDataLoaded) {
    SleepActivity::trimSleepFolderToLimit();
    homeDataLoaded = true;
  }
}

void onGoHome() {
  TransitionFeedback::show(renderer, "Loading home...");
  ensureHomeDataLoaded();
  persistAppState("go home");
  exitActivity();
  enterNewActivity(new HomeActivity(renderer, mappedInputManager, onGoToReader, onGoToMyLibrary, onGoToRecentBooks,
                                    onGoToSettings, onGoToFileTransfer, onGoToBrowser));
}

void setupDisplayAndFonts() {
  display.begin();
  renderer.begin();
  LOG_DBG("MAIN", "Display initialized");
  renderer.insertFont(CHAREINK_14_FONT_ID, chareink14FontFamily);
  renderer.insertFont(CHAREINK_15_FONT_ID, chareink15FontFamily);
  renderer.insertFont(CHAREINK_16_FONT_ID, chareink16FontFamily);
  renderer.insertFont(CHAREINK_17_FONT_ID, chareink17FontFamily);
  renderer.insertFont(CHAREINK_18_FONT_ID, chareink18FontFamily);
  renderer.insertFont(BOOKERLY_13_FONT_ID, bookerly13FontFamily);
  renderer.insertFont(BOOKERLY_15_FONT_ID, bookerly15FontFamily);
  renderer.insertFont(BOOKERLY_18_FONT_ID, bookerly18FontFamily);
  renderer.insertFont(VOLLKORN_15_FONT_ID, vollkorn15FontFamily);
  renderer.insertFont(VOLLKORN_18_FONT_ID, vollkorn18FontFamily);
  renderer.insertFont(GEORGIA_15_FONT_ID, georgia15FontFamily);
  renderer.insertFont(GEORGIA_18_FONT_ID, georgia18FontFamily);
  renderer.insertFont(IMFELL_15_FONT_ID, imfell15FontFamily);
  renderer.insertFont(IMFELL_18_FONT_ID, imfell18FontFamily);
  renderer.insertFont(UNIFONT_14_FONT_ID, unifont14FontFamily);
  renderer.insertFont(UNIFONT_18_FONT_ID, unifont18FontFamily);
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  static FontCacheManager fontCacheManager(renderer.getFontMap());
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  LOG_DBG("MAIN", "Fonts setup");
}

bool ensureCrosspointDataDir() {
  constexpr const char* dataDir = "/.crosspoint";

  if (Storage.exists(dataDir)) {
    FsFile entry = Storage.open(dataDir, O_RDONLY);
    if (!entry) {
      LOG_ERR("MAIN", "Failed to inspect %s", dataDir);
      return false;
    }

    const bool isDirectory = entry.isDirectory();
    entry.close();
    if (!isDirectory) {
      constexpr const char* quarantinePath = "/.crosspoint.corrupt";
      if (Storage.exists(quarantinePath)) {
        if (!Storage.remove(quarantinePath)) {
          Storage.removeDir(quarantinePath);
        }
      }

      if (!Storage.rename(dataDir, quarantinePath)) {
        LOG_ERR("MAIN", "Failed to quarantine invalid %s", dataDir);
        if (!Storage.remove(dataDir)) {
          return false;
        }
      } else {
        LOG_ERR("MAIN", "Quarantined invalid %s to %s", dataDir,
                quarantinePath);
      }
    }
  }

  if (!Storage.mkdir(dataDir)) {
    FsFile dir = Storage.open(dataDir, O_RDONLY);
    if (!dir || !dir.isDirectory()) {
      if (dir) {
        dir.close();
      }
      LOG_ERR("MAIN", "Failed to ensure %s directory", dataDir);
      return false;
    }
    dir.close();
  }

  return true;
}

void setup() {
  t1 = millis();

  gpio.begin();
  powerManager.begin();
  HalSystem::begin();

  // Only start serial if USB connected
  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    // Wait up to 500ms for Serial to be ready (enough for USB CDC enumeration)
    unsigned long start = millis();
    while (!Serial && (millis() - start) < 500) {
      delay(10);
    }
  }

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts();
    exitActivity();
    enterNewActivity(new FullScreenMessageActivity(renderer, mappedInputManager, "SD card error", EpdFontFamily::REGULAR));
    return;
  }

  // Dump crash report to SD card if we rebooted from a panic
  HalSystem::checkPanic();
  HalSystem::clearPanic();

  if (!ensureCrosspointDataDir()) {
    LOG_ERR("MAIN", "Storage layout error: cannot access /.crosspoint");
  }

  SETTINGS.loadFromFile();
  // Retry theme load up to 3 times — a transient SD read failure here would
  // leave the theme list empty, and any later save could overwrite the file.
  for (int attempt = 0; attempt < 3; attempt++) {
    if (READING_THEMES.loadFromFile()) {
      break;
    }
    LOG_ERR("MAIN", "Theme load attempt %d failed, retrying...", attempt + 1);
    delay(50);
  }
  KOREADER_STORE.loadFromFile();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  switch (gpio.getWakeupReason()) {
    case HalGPIO::WakeupReason::PowerButton:
      // For normal wakeups, verify power button press duration
      LOG_DBG("MAIN", "Verifying power button press duration");
      verifyPowerButtonDuration();
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint-Mod-DX34 version " CROSSPOINT_VERSION);

  setupDisplayAndFonts();

  exitActivity();
  auto* bootActivity = new BootActivity(renderer, mappedInputManager);
  enterNewActivity(bootActivity);

  bootActivity->setProgress(32, "Restoring state");
  APP_STATE.loadFromFile();

  LOG_INF("MAIN", "Booting complete, checking initial activity");

  // Determine boot destination: home screen or last open book.
  const bool goHome = APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
                      mappedInputManager.isPressed(MappedInputManager::Button::Back) ||
                      APP_STATE.readerActivityLoadCount > 0;

  // Always load recents early — reader activities call addBook() which saves
  // to disk, so an unloaded list would overwrite the file with just one entry.
  RECENT_BOOKS.loadFromFile();

  // Defer sleep cache trimming until home screen is actually needed.
  if (goHome) {
    bootActivity->setProgress(60, "Refreshing sleep cache");
    SleepActivity::trimSleepFolderToLimit();
    homeDataLoaded = true;
  }
  bootActivity->setProgress(80, goHome ? "Preparing home" : "Resuming book");

  // Capture reader path before we potentially clear it.
  std::string readerPath;
  if (!goHome) {
    readerPath = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
  }

  // Show last sleep wallpaper triage popup before proceeding, if enabled.
  const bool showWallpaperTriage = SETTINGS.showLastSleepWallpaperOnBoot &&
                                   !APP_STATE.lastSleepWallpaperPath.empty() &&
                                   Storage.exists(APP_STATE.lastSleepWallpaperPath.c_str());

  if (goHome) {
    bootActivity->setProgress(100, "Opening home");
    // trimSleepFolderToLimit() already called at line 365, no need to repeat
    if (showWallpaperTriage) {
      exitActivity();
      enterNewActivity(new LastSleepWallpaperActivity(renderer, mappedInputManager, [=]() { onGoHome(); }));
    } else {
      onGoHome();
    }
  } else {
    bootActivity->setProgress(100, "Opening last book");
    if (showWallpaperTriage) {
      exitActivity();
      enterNewActivity(
          new LastSleepWallpaperActivity(renderer, mappedInputManager, [=]() { onGoToReader(readerPath); }));
    } else {
      onGoToReader(readerPath);
    }
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.update();

  // Only update fading fix when it changes (avoid calling every loop iteration)
  {
    static bool lastFadingFix = !SETTINGS.fadingFix;  // Force first-time update
    if (SETTINGS.fadingFix != lastFadingFix) {
      lastFadingFix = SETTINGS.fadingFix;
      renderer.setFadingFix(lastFadingFix);
    }
  }

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes", ESP.getFreeHeap(), ESP.getHeapSize(),
            ESP.getMinFreeHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        logSerial.printf("SCREENSHOT_START:%d\n", HalDisplay::BUFFER_SIZE);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, HalDisplay::BUFFER_SIZE);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || (currentActivity && currentActivity->preventAutoSleep())) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (currentActivity) {
    currentActivity->loop();
  }

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms", maxLoopDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (currentActivity && currentActivity->skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
