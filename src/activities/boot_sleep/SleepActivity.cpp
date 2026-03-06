#include "SleepActivity.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>
#include <algorithm>
#include <unordered_set>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace {
// Returns all .bmp filenames from /sleep in sorted order.
// Does NOT open/validate each file — invalid BMPs are skipped at render time.
std::vector<std::string> getValidSleepBitmaps() {
  std::vector<std::string> files;
  auto dir = Storage.open("/sleep");
  if (!dir || !dir.isDirectory()) {
    if (dir)
      dir.close();
    return files;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }

    file.getName(name, sizeof(name));
    std::string filename(name);
    if (filename.empty() || filename[0] == '.') {
      file.close();
      continue;
    }

    if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".bmp") {
      files.emplace_back(std::move(filename));
    }
    file.close();
  }
  dir.close();

  std::sort(files.begin(), files.end());
  return files;
}

void shuffleSleepPlaylist(std::vector<std::string> &files) {
  if (files.size() <= 1)
    return;
  for (size_t i = files.size() - 1; i > 0; --i) {
    const auto j = static_cast<size_t>(random(i + 1));
    std::swap(files[i], files[j]);
  }
}

void syncSleepPlaylistWithFiles(const std::vector<std::string> &files,
                                bool forceReshuffle) {
  auto &playlist = APP_STATE.sleepImagePlaylist;
  bool changed = false;

  if (files.empty()) {
    if (!playlist.empty()) {
      playlist.clear();
      APP_STATE.saveToFile();
    }
    return;
  }

  if (forceReshuffle) {
    playlist = files;
    shuffleSleepPlaylist(playlist);
    APP_STATE.lastSleepImage = 0;
    APP_STATE.saveToFile();
    return;
  }

  if (playlist.empty()) {
    // Default behavior: follow stable filename order from /sleep directory.
    playlist = files;
    APP_STATE.lastSleepImage = 0;
    APP_STATE.saveToFile();
    return;
  }

  // Build a hash set of on-disk files for O(1) lookup.
  const std::unordered_set<std::string> fileSet(files.begin(), files.end());

  // Remove entries that no longer exist on disk.
  const auto oldSize = playlist.size();
  playlist.erase(
      std::remove_if(playlist.begin(), playlist.end(),
                     [&fileSet](const std::string &e) {
                       return fileSet.count(e) == 0;
                     }),
      playlist.end());
  if (playlist.size() != oldSize) {
    changed = true;
  }

  // Find newly-added files not yet in the playlist.
  const std::unordered_set<std::string> playlistSet(playlist.begin(),
                                                    playlist.end());
  std::vector<std::string> newFiles;
  for (const auto &file : files) {
    if (playlistSet.count(file) == 0) {
      newFiles.push_back(file);
    }
  }

  // Insert new files right after the current head so they show immediately.
  // When lastSleepImage == 0 nothing has been shown yet, so insert at 0.
  // When lastSleepImage == 1 the head (playlist[0]) is the last-shown image
  // and will be rotated to the back before the next render, so insert at 1.
  if (!newFiles.empty()) {
    const size_t insertPos =
        (APP_STATE.lastSleepImage == 0) ? 0 : std::min<size_t>(1, playlist.size());
    playlist.insert(playlist.begin() + insertPos, newFiles.begin(),
                    newFiles.end());
    changed = true;
  }

  if (playlist.empty()) {
    playlist = files;
    APP_STATE.lastSleepImage = 0;
    changed = true;
  }

  if (changed) {
    APP_STATE.saveToFile();
  }
}

// For large collections (> SLEEP_PLAYLIST_MAX_PERSIST) we do not maintain the
// full playlist in memory. Instead we find the next file after the last-shown
// one using a binary search on the already-sorted files list.
std::string nextSleepImageLargeCollection(const std::vector<std::string> &files) {
  const auto &last = APP_STATE.lastShownSleepFilename;
  std::string next;

  if (last.empty()) {
    // No prior context: start from the beginning.
    next = files.front();
  } else if (APP_STATE.lastSleepImage == 0) {
    // randomizeSleepImagePlaylist() set a starting file but nothing rendered
    // yet — show it now without advancing past it.
    next = last;
  } else {
    // Find the last-shown file and advance to the next one, wrapping around.
    auto it = std::lower_bound(files.begin(), files.end(), last);
    if (it != files.end() && *it == last) {
      ++it;
    }
    next = (it != files.end()) ? *it : files.front();
  }

  APP_STATE.lastShownSleepFilename = next;
  APP_STATE.lastSleepImage = 1;
  APP_STATE.saveToFile();
  return next;
}

void drawSleepFilenameLabel(GfxRenderer &renderer, const char *filename) {
  if (!filename || filename[0] == '\0')
    return;

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int safeInset =
      18; // Keep label well inside visible area to avoid bezel clipping.
  const int paddingX = 4;
  const int paddingY = 2;
  const int textLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int maxBoxWidth = std::max(1, screenWidth - safeInset * 2);
  const int maxTextWidth = std::max(1, maxBoxWidth - paddingX * 2 - 2);

  std::string text =
      renderer.truncatedText(UI_10_FONT_ID, filename, maxTextWidth);
  const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, text.c_str(),
                                              EpdFontFamily::REGULAR);
  const int boxWidth = std::min(textWidth + paddingX * 2, maxBoxWidth);
  const int boxHeight = textLineHeight + paddingY * 2;
  const int boxX = safeInset;
  const int boxY = std::max(safeInset, screenHeight - boxHeight - safeInset);
  const int textX = boxX + paddingX;
  const int textY = boxY + paddingY;

  renderer.fillRect(boxX, boxY, boxWidth, boxHeight, true);
  renderer.drawRect(boxX, boxY, boxWidth, boxHeight, false);
  renderer.drawText(UI_10_FONT_ID, textX, textY, text.c_str(), false,
                    EpdFontFamily::REGULAR);
}
} // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();
  GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));

  switch (SETTINGS.sleepScreen) {
  case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
    return renderBlankSleepScreen();
  case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
    return renderCustomSleepScreen();
  case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
  case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
    return renderCoverSleepScreen();
  default:
    return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  const auto files = getValidSleepBitmaps();
  if (!files.empty()) {
    std::string selectedImage;

    if (files.size() > CrossPointState::SLEEP_PLAYLIST_MAX_PERSIST) {
      // Large collection: skip the full in-memory playlist to avoid heap
      // exhaustion. Advance sequentially by filename using a binary search.
      selectedImage = nextSleepImageLargeCollection(files);
    } else {
      // Small collection: maintain the full shuffleable playlist.
      syncSleepPlaylistWithFiles(files, false);
      auto &playlist = APP_STATE.sleepImagePlaylist;
      if (!playlist.empty()) {
        bool changed = false;
        // Advance to the next image only after the first custom sleep render.
        // This keeps playlist[0] as the image just shown and playlist[1] as next.
        if (APP_STATE.lastSleepImage != 0 && playlist.size() > 1) {
          const auto first = playlist.front();
          playlist.erase(playlist.begin());
          playlist.push_back(first);
          changed = true;
        }
        selectedImage = playlist.front();
        if (APP_STATE.lastSleepImage != 1) {
          APP_STATE.lastSleepImage = 1;
          changed = true;
        }
        if (changed) {
          APP_STATE.saveToFile();
        }
      }
    }

    if (!selectedImage.empty()) {
      const auto filename = "/sleep/" + selectedImage;
      FsFile file;
      if (Storage.openFileForRead("SLP", filename, file)) {
        LOG_DBG("SLP", "Loading: %s", filename.c_str());
        delay(100);
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap, selectedImage.c_str());
          file.close();
          return;
        }
        file.close();
      }
    }
  }

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  FsFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap, "sleep.bmp");
      file.close();
      return;
    }
    file.close();
  }

  renderDefaultSleepScreen();
}

bool SleepActivity::randomizeSleepImagePlaylist() {
  const auto files = getValidSleepBitmaps();
  if (files.empty()) {
    if (!APP_STATE.sleepImagePlaylist.empty()) {
      APP_STATE.sleepImagePlaylist.clear();
      APP_STATE.saveToFile();
    }
    return false;
  }

  if (files.size() > CrossPointState::SLEEP_PLAYLIST_MAX_PERSIST) {
    // Large collection: pick a random starting file; sequential advance
    // resumes from there on the next sleep.
    const auto idx = static_cast<size_t>(random(static_cast<long>(files.size())));
    APP_STATE.sleepImagePlaylist.clear();
    APP_STATE.lastShownSleepFilename = files[idx];
    APP_STATE.lastSleepImage = 0;
    APP_STATE.saveToFile();
    return true;
  }

  syncSleepPlaylistWithFiles(files, true);
  return true;
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10,
                            tr(STR_CROSSPOINT), true, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 15,
                            tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap &bitmap,
                                            const char *sourceFilename) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(),
          bitmap.getHeight(), pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) /
                  static_cast<float>(bitmap.getHeight());
    const float screenRatio =
        static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered
      // vertically
      if (SETTINGS.sleepScreenCoverMode ==
          CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) /
                static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) -
                      static_cast<float>(pageWidth) / ratio) /
                     2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be
      // centered horizontally
      if (SETTINGS.sleepScreenCoverMode ==
          CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) /
                ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) -
                      static_cast<float>(pageHeight) * ratio) /
                     2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  renderer.clearScreen();

  const bool hasGreyscale =
      bitmap.hasGreyscale() &&
      SETTINGS.sleepScreenCoverFilter ==
          CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter ==
      CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (SETTINGS.showSleepImageFilename && sourceFilename != nullptr) {
    drawSleepFilenameLabel(renderer, sourceFilename);
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
  case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
    renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
    break;
  default:
    renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
    break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode ==
                 CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".xtc") ||
      StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".xtch")) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".txt")) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".epub")) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  FsFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      file.close();
      return;
    }
    file.close();
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
