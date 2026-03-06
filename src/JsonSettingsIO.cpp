#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "RecentBooksStore.h"
#include "WifiCredentialStore.h"

namespace {
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
} // namespace

// ---- Atomic write & read helpers ----
// FAT32 does not support atomic rename over existing files.
// To prevent dataloss on crash, we rotate backups:
// 1. Write to .tmp
// 2. Erase old .bak
// 3. Rename current to .bak
// 4. Rename .tmp to current
static bool safeWriteFile(const char *path, const String &json) {
  auto ensureParentDirectory = [](const char *targetPath) -> bool {
    const char *slash = strrchr(targetPath, '/');
    if (!slash || slash == targetPath) {
      return true;
    }

    char parentPath[128];
    const size_t parentLen = static_cast<size_t>(slash - targetPath);
    if (parentLen >= sizeof(parentPath)) {
      LOG_ERR("JSN", "safeWriteFile: parent path too long for %s", targetPath);
      return false;
    }

    memcpy(parentPath, targetPath, parentLen);
    parentPath[parentLen] = '\0';

    if (Storage.exists(parentPath)) {
      FsFile entry = Storage.open(parentPath, O_RDONLY);
      if (entry) {
        const bool isDirectory = entry.isDirectory();
        entry.close();
        if (isDirectory) {
          return true;
        }
      }

      char quarantinePath[160];
      snprintf(quarantinePath, sizeof(quarantinePath), "%s.corrupt", parentPath);
      if (Storage.exists(quarantinePath)) {
        if (!Storage.remove(quarantinePath)) {
          Storage.removeDir(quarantinePath);
        }
      }

      if (!Storage.rename(parentPath, quarantinePath)) {
        if (!Storage.remove(parentPath)) {
          LOG_ERR("JSN",
                  "safeWriteFile: failed to quarantine invalid parent %s",
                  parentPath);
          return false;
        }
      } else {
        LOG_ERR("JSN", "safeWriteFile: quarantined invalid parent %s",
                parentPath);
      }
    }

    if (!Storage.mkdir(parentPath)) {
      FsFile dir = Storage.open(parentPath, O_RDONLY);
      if (!dir || !dir.isDirectory()) {
        if (dir) {
          dir.close();
        }
        LOG_ERR("JSN", "safeWriteFile: failed to create parent %s", parentPath);
        return false;
      }
      dir.close();
    }

    return true;
  };

  if (!ensureParentDirectory(path)) {
    return false;
  }

  char tmpPath[128];
  char bakPath[128];
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
  snprintf(bakPath, sizeof(bakPath), "%s.bak", path);

  // 1. Write to temp file.
  // Explicitly remove any stale .tmp from a previous interrupted write.
  // If the entry is so corrupted it can't even be deleted, fall back to an
  // alternate temp name so saves keep working.
  const char *activeTmp = tmpPath;
  char altTmpPath[128];
  if (Storage.exists(tmpPath)) {
    if (!Storage.remove(tmpPath)) {
      LOG_ERR("JSN",
              "safeWriteFile: stale tmp %s stuck (cannot remove); "
              "falling back to alternate tmp name",
              tmpPath);
      snprintf(altTmpPath, sizeof(altTmpPath), "%s.tmp2", path);
      activeTmp = altTmpPath;
    }
  }
  if (!Storage.writeFile(activeTmp, json)) {
    LOG_ERR("JSN", "safeWriteFile: failed to write tmp %s", activeTmp);
    return false;
  }

  // 2. Remove stale backup
  if (Storage.exists(bakPath)) {
    if (!Storage.remove(bakPath)) {
      LOG_ERR("JSN", "safeWriteFile: failed to remove stale bak %s", bakPath);
    }
  }

  // 3. Rotate current to backup
  if (Storage.exists(path)) {
    if (!Storage.rename(path, bakPath)) {
      LOG_ERR("JSN", "safeWriteFile: failed to rotate %s to %s", path, bakPath);
      Storage.remove(activeTmp);
      return false;
    }
  }

  // 4. Promote tmp to current
  if (!Storage.rename(activeTmp, path)) {
    LOG_ERR("JSN", "safeWriteFile: failed to promote %s", activeTmp);
    if (Storage.exists(bakPath)) {
      if (Storage.exists(path)) {
        Storage.remove(path);
      }
      if (Storage.rename(bakPath, path)) {
        LOG_ERR("JSN", "safeWriteFile: restored %s from backup", path);
      } else {
        LOG_ERR("JSN", "safeWriteFile: failed to restore %s from backup %s",
                path, bakPath);
      }
    }
    return false;
  }

  return true;
}

// Read the JSON file, automatically trying fallbacks if a crash occurred
// mid-save
String JsonSettingsIO::safeReadFile(const char *path) {
  if (Storage.exists(path)) {
    String json = Storage.readFile(path);
    if (!json.isEmpty())
      return json;
  }

  // Primary failed/empty. Try backup (which means crash happened during step 4
  // or 3)
  char bakPath[128];
  snprintf(bakPath, sizeof(bakPath), "%s.bak", path);
  if (Storage.exists(bakPath)) {
    String json = Storage.readFile(bakPath);
    if (!json.isEmpty()) {
      LOG_DBG("JSN", "safeReadFile: Recovered %s from .bak", path);
      return json;
    }
  }

  // Backup failed/empty. Try tmp (which means crash happened during step 1 or
  // 2, but primary was also missing beforehand)
  char tmpPath[128];
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
  if (Storage.exists(tmpPath)) {
    String json = Storage.readFile(tmpPath);
    if (!json.isEmpty()) {
      LOG_DBG("JSN", "safeReadFile: Recovered %s from .tmp", path);
      return json;
    }
  }

  return "";
}

// ---- CrossPointState ----

bool JsonSettingsIO::saveState(const CrossPointState &s, const char *path) {
  JsonDocument doc;
  doc["openEpubPath"] = s.openEpubPath;
  doc["lastSleepImage"] = s.lastSleepImage;
  doc["lastShownSleepFilename"] = s.lastShownSleepFilename;
  doc["readerActivityLoadCount"] = s.readerActivityLoadCount;
  doc["lastSleepFromReader"] = s.lastSleepFromReader;
  // Only persist the playlist for small collections. Large collections track
  // position by filename alone to avoid heap exhaustion and huge state files.
  if (s.sleepImagePlaylist.size() <= CrossPointState::SLEEP_PLAYLIST_MAX_PERSIST) {
    JsonArray sleepImagePlaylist = doc["sleepImagePlaylist"].to<JsonArray>();
    for (const auto &entry : s.sleepImagePlaylist) {
      sleepImagePlaylist.add(entry);
    }
  }

  String json;
  serializeJson(doc, json);
  return safeWriteFile(path, json);
}

bool JsonSettingsIO::loadState(CrossPointState &s, const char *json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    return false;
  }

  s.openEpubPath = doc["openEpubPath"] | std::string("");
  s.lastSleepImage = doc["lastSleepImage"] | (uint8_t)0;
  s.lastShownSleepFilename = doc["lastShownSleepFilename"] | std::string("");
  s.readerActivityLoadCount = doc["readerActivityLoadCount"] | (uint8_t)0;
  s.lastSleepFromReader = doc["lastSleepFromReader"] | false;
  s.sleepImagePlaylist.clear();
  if (doc["sleepImagePlaylist"].is<JsonArray>()) {
    for (const JsonVariant value : doc["sleepImagePlaylist"].as<JsonArray>()) {
      const char *entry = value.as<const char *>();
      if (entry != nullptr && entry[0] != '\0') {
        s.sleepImagePlaylist.emplace_back(entry);
        // Cap on load to protect against legacy large playlists causing OOM.
        if (s.sleepImagePlaylist.size() >= CrossPointState::SLEEP_PLAYLIST_MAX_PERSIST) {
          break;
        }
      }
    }
  }
  return true;
}

// ---- CrossPointSettings ----

bool JsonSettingsIO::saveSettings(const CrossPointSettings &s,
                                  const char *path) {
  JsonDocument doc;

  doc["sleepScreen"] = s.sleepScreen;
  doc["sleepScreenCoverMode"] = s.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = s.sleepScreenCoverFilter;
  doc["showSleepImageFilename"] = s.showSleepImageFilename;
  doc["statusBar"] = s.statusBar;
  doc["statusBarEnabled"] = s.statusBarEnabled;
  doc["statusBarShowBattery"] = s.statusBarShowBattery;
  doc["statusBarShowPageCounter"] = s.statusBarShowPageCounter;
  doc["statusBarShowBookPercentage"] = s.statusBarShowBookPercentage;
  doc["statusBarShowChapterPercentage"] = s.statusBarShowChapterPercentage;
  doc["statusBarShowBookBar"] = s.statusBarShowBookBar;
  doc["statusBarShowChapterBar"] = s.statusBarShowChapterBar;
  doc["statusBarShowChapterTitle"] = s.statusBarShowChapterTitle;
  doc["statusBarTopLine"] = s.statusBarTopLine;
  doc["statusBarTextAlignment"] = s.statusBarTextAlignment;
  doc["statusBarProgressStyle"] = s.statusBarProgressStyle;
  doc["extraParagraphSpacingLevel"] = s.extraParagraphSpacingLevel;
  // Legacy compatibility key for older builds that still expect a toggle.
  doc["extraParagraphSpacing"] =
      s.extraParagraphSpacingLevel != CrossPointSettings::EXTRA_SPACING_OFF;
  doc["textAntiAliasing"] = s.textAntiAliasing;
  doc["shortPwrBtn"] = s.shortPwrBtn;
  doc["orientation"] = s.orientation;
  doc["sideButtonLayout"] = s.sideButtonLayout;
  doc["frontButtonBack"] = s.frontButtonBack;
  doc["frontButtonConfirm"] = s.frontButtonConfirm;
  doc["frontButtonLeft"] = s.frontButtonLeft;
  doc["frontButtonRight"] = s.frontButtonRight;
  doc["fontFamily"] = s.fontFamily;
  doc["fontSize"] = s.fontSize;
  doc["lineSpacing"] = s.lineSpacing;
  doc["lineSpacingPercent"] = s.lineSpacingPercent;
  doc["paragraphAlignment"] = s.paragraphAlignment;
  doc["sleepTimeout"] = s.sleepTimeout;
  doc["refreshFrequency"] = s.refreshFrequency;
  doc["screenMargin"] = s.screenMargin;
  doc["screenMarginHorizontal"] = s.screenMarginHorizontal;
  doc["screenMarginTop"] = s.screenMarginTop;
  doc["screenMarginBottom"] = s.screenMarginBottom;
  doc["opdsServerUrl"] = s.opdsServerUrl;
  doc["opdsUsername"] = s.opdsUsername;
  doc["opdsPassword_obf"] = obfuscation::obfuscateToBase64(s.opdsPassword);
  doc["hideBatteryPercentage"] = s.hideBatteryPercentage;
  doc["longPressChapterSkip"] = s.longPressChapterSkip;
  doc["hyphenationEnabled"] = s.hyphenationEnabled;
  doc["readerBoldSwap"] = s.readerBoldSwap;
  doc["fadingFix"] = s.fadingFix;
  doc["embeddedStyle"] = s.embeddedStyle;
  doc["debugBorders"] = s.debugBorders;

  String json;
  serializeJson(doc, json);
  return safeWriteFile(path, json);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings &s, const char *json,
                                  bool *needsResave) {
  if (needsResave)
    *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    return false;
  }

  using S = CrossPointSettings;
  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t {
    return val < maxVal ? val : def;
  };

  s.sleepScreen = clamp(doc["sleepScreen"] | (uint8_t)S::DARK,
                        S::SLEEP_SCREEN_MODE_COUNT, S::DARK);
  s.sleepScreenCoverMode = clamp(doc["sleepScreenCoverMode"] | (uint8_t)S::FIT,
                                 S::SLEEP_SCREEN_COVER_MODE_COUNT, S::FIT);
  s.sleepScreenCoverFilter =
      clamp(doc["sleepScreenCoverFilter"] | (uint8_t)S::NO_FILTER,
            S::SLEEP_SCREEN_COVER_FILTER_COUNT, S::NO_FILTER);
  s.showSleepImageFilename = doc["showSleepImageFilename"] | (uint8_t)0;
  s.statusBar = clamp(doc["statusBar"] | (uint8_t)S::FULL,
                      S::STATUS_BAR_MODE_COUNT, S::FULL);
  const bool hasGranularStatusBar = !doc["statusBarEnabled"].isNull() &&
                                    !doc["statusBarShowBattery"].isNull() &&
                                    !doc["statusBarShowPageCounter"].isNull() &&
                                    !doc["statusBarShowBookPercentage"].isNull() &&
                                    !doc["statusBarShowChapterPercentage"].isNull() &&
                                    !doc["statusBarShowBookBar"].isNull() &&
                                    !doc["statusBarShowChapterBar"].isNull() &&
                                    !doc["statusBarShowChapterTitle"].isNull() &&
                                    !doc["statusBarTopLine"].isNull() &&
                                    !doc["statusBarTextAlignment"].isNull() &&
                                    !doc["statusBarProgressStyle"].isNull();
  if (hasGranularStatusBar) {
    s.statusBarEnabled = doc["statusBarEnabled"] | (uint8_t)1;
    s.statusBarShowBattery = doc["statusBarShowBattery"] | (uint8_t)1;
    s.statusBarShowPageCounter = doc["statusBarShowPageCounter"] | (uint8_t)0;
    s.statusBarShowBookPercentage =
        doc["statusBarShowBookPercentage"] | (uint8_t)0;
    s.statusBarShowChapterPercentage =
        doc["statusBarShowChapterPercentage"] | (uint8_t)0;
    s.statusBarShowBookBar = doc["statusBarShowBookBar"] | (uint8_t)0;
    s.statusBarShowChapterBar = doc["statusBarShowChapterBar"] | (uint8_t)0;
    s.statusBarShowChapterTitle = doc["statusBarShowChapterTitle"] | (uint8_t)1;
    s.statusBarTopLine = doc["statusBarTopLine"] | (uint8_t)0;
    s.statusBarTextAlignment =
        clamp(doc["statusBarTextAlignment"] | (uint8_t)S::STATUS_TEXT_RIGHT,
              S::STATUS_TEXT_ALIGNMENT_COUNT, S::STATUS_TEXT_RIGHT);
    s.statusBarProgressStyle =
        clamp(doc["statusBarProgressStyle"] | (uint8_t)S::STATUS_BAR_THICK,
              S::STATUS_BAR_PROGRESS_STYLE_COUNT, S::STATUS_BAR_THICK);
  } else {
    migrateLegacyStatusBarMode(s);
    if (needsResave)
      *needsResave = true;
  }
  if (!doc["extraParagraphSpacingLevel"].isNull()) {
    s.extraParagraphSpacingLevel = clamp(
        doc["extraParagraphSpacingLevel"] | (uint8_t)S::EXTRA_SPACING_M,
        S::EXTRA_PARAGRAPH_SPACING_COUNT, S::EXTRA_SPACING_M);
  } else {
    const uint8_t legacyExtraSpacing = doc["extraParagraphSpacing"] | (uint8_t)1;
    s.extraParagraphSpacingLevel = legacyExtraSpacing
                                       ? (uint8_t)S::EXTRA_SPACING_M
                                       : (uint8_t)S::EXTRA_SPACING_OFF;
    if (needsResave)
      *needsResave = true;
  }
  s.textAntiAliasing = doc["textAntiAliasing"] | (uint8_t)1;
  s.shortPwrBtn = clamp(doc["shortPwrBtn"] | (uint8_t)S::IGNORE,
                        S::SHORT_PWRBTN_COUNT, S::IGNORE);
  s.orientation = clamp(doc["orientation"] | (uint8_t)S::PORTRAIT,
                        S::ORIENTATION_COUNT, S::PORTRAIT);
  s.sideButtonLayout = clamp(doc["sideButtonLayout"] | (uint8_t)S::PREV_NEXT,
                             S::SIDE_BUTTON_LAYOUT_COUNT, S::PREV_NEXT);
  s.frontButtonBack = clamp(doc["frontButtonBack"] | (uint8_t)S::FRONT_HW_BACK,
                            S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_BACK);
  s.frontButtonConfirm =
      clamp(doc["frontButtonConfirm"] | (uint8_t)S::FRONT_HW_CONFIRM,
            S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_CONFIRM);
  s.frontButtonLeft = clamp(doc["frontButtonLeft"] | (uint8_t)S::FRONT_HW_LEFT,
                            S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_LEFT);
  s.frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)S::FRONT_HW_RIGHT,
            S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_RIGHT);
  CrossPointSettings::validateFrontButtonMapping(s);
  // DX34 keeps only ChareInk; migrate any stored value to that.
  s.fontFamily = S::CHAREINK;
  s.fontSize = clamp(doc["fontSize"] | (uint8_t)S::MEDIUM, S::FONT_SIZE_COUNT,
                     S::MEDIUM);
  s.lineSpacing = clamp(doc["lineSpacing"] | (uint8_t)S::NORMAL,
                        S::LINE_COMPRESSION_COUNT, S::NORMAL);
  if (!doc["lineSpacingPercent"].isNull()) {
    const uint8_t parsed = doc["lineSpacingPercent"] | (uint8_t)110;
    if (parsed < 65) {
      s.lineSpacingPercent = 65;
    } else if (parsed > 150) {
      s.lineSpacingPercent = 150;
    } else {
      s.lineSpacingPercent = parsed;
    }
  } else {
    switch (s.lineSpacing) {
    case S::TIGHT:
      s.lineSpacingPercent = 95;
      break;
    case S::WIDE:
      s.lineSpacingPercent = 125;
      break;
    case S::NORMAL:
    default:
      s.lineSpacingPercent = 110;
      break;
    }
    if (needsResave) {
      *needsResave = true;
    }
  }
  s.paragraphAlignment =
      clamp(doc["paragraphAlignment"] | (uint8_t)S::JUSTIFIED,
            S::PARAGRAPH_ALIGNMENT_COUNT, S::JUSTIFIED);
  s.sleepTimeout = clamp(doc["sleepTimeout"] | (uint8_t)S::SLEEP_10_MIN,
                         S::SLEEP_TIMEOUT_COUNT, S::SLEEP_10_MIN);
  s.refreshFrequency = clamp(doc["refreshFrequency"] | (uint8_t)S::REFRESH_15,
                             S::REFRESH_FREQUENCY_COUNT, S::REFRESH_15);
  s.screenMargin = doc["screenMargin"] | (uint8_t)5;
  const bool hasSplitMargins = !doc["screenMarginHorizontal"].isNull() &&
                               !doc["screenMarginTop"].isNull() &&
                               !doc["screenMarginBottom"].isNull();
  if (hasSplitMargins) {
    s.screenMarginHorizontal = doc["screenMarginHorizontal"] | s.screenMargin;
    s.screenMarginTop = doc["screenMarginTop"] | s.screenMargin;
    s.screenMarginBottom = doc["screenMarginBottom"] | s.screenMargin;
  } else {
    s.screenMarginHorizontal = s.screenMargin;
    s.screenMarginTop = s.screenMargin;
    s.screenMarginBottom = s.screenMargin;
    if (needsResave)
      *needsResave = true;
  }
  s.hideBatteryPercentage =
      clamp(doc["hideBatteryPercentage"] | (uint8_t)S::HIDE_NEVER,
            S::HIDE_BATTERY_PERCENTAGE_COUNT, S::HIDE_NEVER);
  s.longPressChapterSkip = doc["longPressChapterSkip"] | (uint8_t)1;
  s.hyphenationEnabled = doc["hyphenationEnabled"] | (uint8_t)0;
  s.readerBoldSwap = doc["readerBoldSwap"] | (uint8_t)0;
  s.fadingFix = doc["fadingFix"] | (uint8_t)0;
  s.embeddedStyle = doc["embeddedStyle"] | (uint8_t)1;
  s.debugBorders = doc["debugBorders"] | (uint8_t)0;

  const char *url = doc["opdsServerUrl"] | "";
  strncpy(s.opdsServerUrl, url, sizeof(s.opdsServerUrl) - 1);
  s.opdsServerUrl[sizeof(s.opdsServerUrl) - 1] = '\0';

  const char *user = doc["opdsUsername"] | "";
  strncpy(s.opdsUsername, user, sizeof(s.opdsUsername) - 1);
  s.opdsUsername[sizeof(s.opdsUsername) - 1] = '\0';

  bool passOk = false;
  std::string pass =
      obfuscation::deobfuscateFromBase64(doc["opdsPassword_obf"] | "", &passOk);
  if (!passOk || pass.empty()) {
    pass = doc["opdsPassword"] | "";
    if (!pass.empty() && needsResave)
      *needsResave = true;
  }
  strncpy(s.opdsPassword, pass.c_str(), sizeof(s.opdsPassword) - 1);
  s.opdsPassword[sizeof(s.opdsPassword) - 1] = '\0';
  LOG_DBG("CPS", "Settings loaded from file");
  return true;
}

// ---- KOReaderCredentialStore ----

bool JsonSettingsIO::saveKOReader(const KOReaderCredentialStore &store,
                                  const char *path) {
  JsonDocument doc;
  doc["username"] = store.getUsername();
  doc["password_obf"] = obfuscation::obfuscateToBase64(store.getPassword());
  doc["serverUrl"] = store.getServerUrl();
  doc["matchMethod"] = static_cast<uint8_t>(store.getMatchMethod());

  String json;
  serializeJson(doc, json);
  return safeWriteFile(path, json);
}

bool JsonSettingsIO::loadKOReader(KOReaderCredentialStore &store,
                                  const char *json, bool *needsResave) {
  if (needsResave)
    *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("KRS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.username = doc["username"] | std::string("");
  bool ok = false;
  store.password =
      obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
  if (!ok || store.password.empty()) {
    store.password = doc["password"] | std::string("");
    if (!store.password.empty() && needsResave)
      *needsResave = true;
  }
  store.serverUrl = doc["serverUrl"] | std::string("");
  uint8_t method = doc["matchMethod"] | (uint8_t)0;
  store.matchMethod = static_cast<DocumentMatchMethod>(method);

  LOG_DBG("KRS", "Loaded KOReader credentials for user: %s",
          store.username.c_str());
  return true;
}

// ---- WifiCredentialStore ----

bool JsonSettingsIO::saveWifi(const WifiCredentialStore &store,
                              const char *path) {
  JsonDocument doc;
  doc["lastConnectedSsid"] = store.getLastConnectedSsid();

  JsonArray arr = doc["credentials"].to<JsonArray>();
  for (const auto &cred : store.getCredentials()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = cred.ssid;
    obj["password_obf"] = obfuscation::obfuscateToBase64(cred.password);
  }

  String json;
  serializeJson(doc, json);
  return safeWriteFile(path, json);
}

bool JsonSettingsIO::loadWifi(WifiCredentialStore &store, const char *json,
                              bool *needsResave) {
  if (needsResave)
    *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("WCS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.lastConnectedSsid = doc["lastConnectedSsid"] | std::string("");

  store.credentials.clear();
  JsonArray arr = doc["credentials"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.credentials.size() >= store.MAX_NETWORKS)
      break;
    WifiCredential cred;
    cred.ssid = obj["ssid"] | std::string("");
    bool ok = false;
    cred.password =
        obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
    if (!ok || cred.password.empty()) {
      cred.password = obj["password"] | std::string("");
      if (!cred.password.empty() && needsResave)
        *needsResave = true;
    }
    store.credentials.push_back(cred);
  }

  LOG_DBG("WCS", "Loaded %zu WiFi credentials from file",
          store.credentials.size());
  return true;
}

// ---- RecentBooksStore ----

bool JsonSettingsIO::saveRecentBooks(const RecentBooksStore &store,
                                     const char *path) {
  JsonDocument doc;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto &book : store.getBooks()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }

  String json;
  serializeJson(doc, json);
  return safeWriteFile(path, json);
}

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore &store,
                                     const char *json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("RBS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.recentBooks.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.getCount() >= RecentBooksStore::MAX_RECENT_BOOKS)
      break;
    RecentBook book;
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.recentBooks.push_back(book);
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries)",
          store.getCount());
  return true;
}
