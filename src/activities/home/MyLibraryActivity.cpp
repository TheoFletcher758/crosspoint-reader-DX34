#include "MyLibraryActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookProgress.h"
#include "util/StringUtils.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;

const char* const FILE_ACTION_LABELS[] = {"Open", "Move File", "Delete File", "Cancel"};
constexpr int FILE_ACTION_COUNT = sizeof(FILE_ACTION_LABELS) / sizeof(FILE_ACTION_LABELS[0]);

std::string rtrimSpaces(std::string text) {
  while (!text.empty() && text.back() == ' ') {
    text.pop_back();
  }
  return text;
}

std::vector<std::string> wrapTextToWidth(const GfxRenderer& renderer, const int fontId, const std::string& text,
                                         const int maxWidth) {
  if (text.empty()) return {""};

  std::vector<std::string> lines;
  size_t pos = 0;

  while (pos < text.size()) {
    while (pos < text.size() && text[pos] == ' ') pos++;
    if (pos >= text.size()) break;

    size_t end = pos;
    size_t lastSpace = std::string::npos;

    while (end < text.size()) {
      if (text[end] == ' ') lastSpace = end;
      const std::string candidate = text.substr(pos, end - pos + 1);
      if (renderer.getTextWidth(fontId, candidate.c_str()) > maxWidth) break;
      end++;
    }

    if (end >= text.size()) {
      lines.push_back(rtrimSpaces(text.substr(pos)));
      break;
    }

    if (end == pos) {
      // Force at least one character so very long unbroken tokens still wrap.
      size_t forcedEnd = pos + 1;
      while (forcedEnd < text.size()) {
        const std::string forcedCandidate = text.substr(pos, forcedEnd - pos + 1);
        if (renderer.getTextWidth(fontId, forcedCandidate.c_str()) > maxWidth) break;
        forcedEnd++;
      }
      lines.push_back(rtrimSpaces(text.substr(pos, forcedEnd - pos)));
      pos = forcedEnd;
      continue;
    }

    size_t split = (lastSpace != std::string::npos && lastSpace >= pos) ? lastSpace : (end - 1);
    lines.push_back(rtrimSpaces(text.substr(pos, split - pos + 1)));
    pos = split + 1;
  }

  if (lines.empty()) lines.push_back("");
  return lines;
}
}  // namespace

void sortFileList(std::vector<std::string>& strs) {
  std::sort(begin(strs), end(strs), [](const std::string& str1, const std::string& str2) {
    // Directories first
    bool isDir1 = str1.back() == '/';
    bool isDir2 = str2.back() == '/';
    if (isDir1 != isDir2) return isDir1;

    // Start naive natural sort
    const char* s1 = str1.c_str();
    const char* s2 = str2.c_str();

    // Iterate while both strings have characters
    while (*s1 && *s2) {
      // Check if both are at the start of a number
      if (isdigit(*s1) && isdigit(*s2)) {
        // Skip leading zeros and track them
        const char* start1 = s1;
        const char* start2 = s2;
        while (*s1 == '0') s1++;
        while (*s2 == '0') s2++;

        // Count digits to compare lengths first
        int len1 = 0, len2 = 0;
        while (isdigit(s1[len1])) len1++;
        while (isdigit(s2[len2])) len2++;

        // Different length so return smaller integer value
        if (len1 != len2) return len1 < len2;

        // Same length so compare digit by digit
        for (int i = 0; i < len1; i++) {
          if (s1[i] != s2[i]) return s1[i] < s2[i];
        }

        // Numbers equal so advance pointers
        s1 += len1;
        s2 += len2;
      } else {
        // Regular case-insensitive character comparison
        char c1 = tolower(*s1);
        char c2 = tolower(*s2);
        if (c1 != c2) return c1 < c2;
        s1++;
        s2++;
      }
    }

    // One string is prefix of other
    return *s1 == '\0' && *s2 != '\0';
  });
}

void orderSleepFolderByPlaylist(std::vector<std::string>& entries) {
  // In /sleep, prioritize BMP files by the persisted sleep playlist order.
  // The playlist stores the most recently displayed image at index 0.
  if (APP_STATE.sleepImagePlaylist.empty()) {
    return;
  }

  std::vector<std::string> directories;
  std::vector<std::string> orderedBmps;
  std::vector<std::string> remaining;

  for (const auto& name : entries) {
    if (!name.empty() && name.back() == '/') {
      directories.push_back(name);
    }
  }

  for (const auto& playlistName : APP_STATE.sleepImagePlaylist) {
    const auto it = std::find(entries.begin(), entries.end(), playlistName);
    if (it != entries.end()) {
      orderedBmps.push_back(*it);
    }
  }

  for (const auto& name : entries) {
    if (!name.empty() && name.back() == '/') {
      continue;
    }
    if (std::find(orderedBmps.begin(), orderedBmps.end(), name) != orderedBmps.end()) {
      continue;
    }
    remaining.push_back(name);
  }

  entries.clear();
  entries.reserve(directories.size() + orderedBmps.size() + remaining.size());
  entries.insert(entries.end(), directories.begin(), directories.end());
  entries.insert(entries.end(), orderedBmps.begin(), orderedBmps.end());
  entries.insert(entries.end(), remaining.begin(), remaining.end());
}

void MyLibraryActivity::loadFiles() {
  files.clear();
  progressPrefixCache.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();

  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] == '.' || strcmp(name, "System Volume Information") == 0) {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(std::string(name) + "/");
    } else {
      auto filename = std::string(name);
      if (isManagedFile(filename)) {
        files.emplace_back(filename);
      }
    }
    file.close();
  }
  root.close();
  sortFileList(files);
  if (basepath == "/sleep") {
    orderSleepFolderByPlaylist(files);
  }

  for (const auto& name : files) {
    if (!name.empty() && name.back() == '/') {
      continue;
    }
    if (!isBookFile(name)) {
      continue;
    }
    const std::string fullPath = makeAbsolutePath(name);
    progressPrefixCache.emplace(fullPath, BookProgress::getPrefix(fullPath));
  }
}

std::string MyLibraryActivity::makeAbsolutePath(const std::string& name) const {
  std::string fullPath = basepath;
  if (fullPath.empty() || fullPath.back() != '/') {
    fullPath += "/";
  }
  return fullPath + name;
}

std::string MyLibraryActivity::getBasename(const std::string& path) {
  const auto slashPos = path.find_last_of('/');
  return (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);
}

std::string MyLibraryActivity::getParentPath(const std::string& path) {
  if (path.empty() || path == "/") return "/";
  std::string trimmed = path;
  if (trimmed.back() == '/' && trimmed.size() > 1) trimmed.pop_back();
  const auto slashPos = trimmed.find_last_of('/');
  if (slashPos == std::string::npos || slashPos == 0) return "/";
  return trimmed.substr(0, slashPos);
}

bool MyLibraryActivity::isBookFile(const std::string& filename) {
  return StringUtils::checkFileExtension(filename, ".epub") || StringUtils::checkFileExtension(filename, ".xtch") ||
         StringUtils::checkFileExtension(filename, ".xtc") || StringUtils::checkFileExtension(filename, ".txt") ||
         StringUtils::checkFileExtension(filename, ".md");
}

bool MyLibraryActivity::isBmpFile(const std::string& filename) {
  return StringUtils::checkFileExtension(filename, ".bmp");
}

bool MyLibraryActivity::isManagedFile(const std::string& filename) { return isBookFile(filename) || isBmpFile(filename); }

std::string MyLibraryActivity::getDisplayNameForEntry(const size_t index) {
  if (index >= files.size()) {
    return "";
  }

  const std::string& name = files[index];
  if (!name.empty() && name.back() == '/') {
    return name;
  }

  if (!isBookFile(name)) {
    return name;
  }
  return name;
}

void MyLibraryActivity::enterBmpView(const std::string& bmpPath) {
  selectedFilePath = bmpPath;
  mode = Mode::BMP_VIEW;
  requestUpdate();
}

void MyLibraryActivity::enterFileActions(const std::string& filePath) {
  selectedFilePath = filePath;
  fileActionIndex = 0;
  mode = Mode::FILE_ACTIONS;
  requestUpdate();
}

void MyLibraryActivity::enterFileMoveBrowser() {
  moveBrowserPath = basepath.empty() ? "/" : basepath;
  if (moveBrowserPath.back() == '/' && moveBrowserPath.size() > 1) {
    moveBrowserPath.pop_back();
  }
  fileMoveIndex = 0;
  loadMoveBrowseEntries();
  mode = Mode::FILE_MOVE_BROWSER;
}

void MyLibraryActivity::loadMoveBrowseEntries() {
  moveBrowseEntries.clear();

  if (moveBrowserPath != "/") {
    moveBrowseEntries.push_back({"[..]", getParentPath(moveBrowserPath), true, false});
  }

  auto dir = Storage.open(moveBrowserPath.c_str());
  if (dir && dir.isDirectory()) {
    char name[500];
    std::vector<std::string> directories;
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      if (name[0] == '.' || strcmp(name, "System Volume Information") == 0) {
        file.close();
        continue;
      }
      if (file.isDirectory()) {
        directories.emplace_back(std::string(name) + "/");
      }
      file.close();
    }
    dir.close();
    sortFileList(directories);

    for (const auto& entry : directories) {
      const std::string dirName = entry.substr(0, entry.size() - 1);
      std::string path = moveBrowserPath;
      if (path.empty() || path.back() != '/') path += "/";
      path += dirName;
      moveBrowseEntries.push_back({entry, path, false, false});
    }
  } else if (dir) {
    dir.close();
  }

  moveBrowseEntries.push_back({"[Move Here]", moveBrowserPath, false, true});
  if (fileMoveIndex >= static_cast<int>(moveBrowseEntries.size())) {
    fileMoveIndex = std::max(0, static_cast<int>(moveBrowseEntries.size()) - 1);
  }
}

bool MyLibraryActivity::copyFile(const std::string& srcPath, const std::string& dstPath) const {
  FsFile src;
  if (!Storage.openFileForRead("LIB", srcPath, src)) {
    return false;
  }

  FsFile dst;
  if (!Storage.openFileForWrite("LIB", dstPath, dst)) {
    src.close();
    return false;
  }

  uint8_t buffer[1024];
  while (src.available()) {
    const auto bytesRead = src.read(buffer, sizeof(buffer));
    if (bytesRead == 0) break;
    if (dst.write(buffer, bytesRead) != bytesRead) {
      src.close();
      dst.close();
      return false;
    }
  }

  src.close();
  dst.close();
  return true;
}

bool MyLibraryActivity::moveSelectedFileTo(const std::string& targetDir) const {
  if (selectedFilePath.empty()) return false;

  std::string normalizedTarget = targetDir;
  if (normalizedTarget.empty()) normalizedTarget = "/";
  if (normalizedTarget.back() != '/') normalizedTarget += "/";

  const std::string filename = getBasename(selectedFilePath);
  const std::string destination = normalizedTarget + filename;

  if (destination == selectedFilePath || Storage.exists(destination.c_str())) {
    return false;
  }

  if (!copyFile(selectedFilePath, destination)) {
    Storage.remove(destination.c_str());
    return false;
  }

  if (!Storage.remove(selectedFilePath.c_str())) {
    Storage.remove(destination.c_str());
    return false;
  }

  if (isBookFile(selectedFilePath)) {
    RECENT_BOOKS.removeBook(selectedFilePath);
  }
  return true;
}

bool MyLibraryActivity::deleteSelectedFile() {
  if (selectedFilePath.empty()) return false;
  if (isBookFile(selectedFilePath)) {
    RECENT_BOOKS.removeBook(selectedFilePath);
    if (StringUtils::checkFileExtension(selectedFilePath, ".epub")) {
      Epub(selectedFilePath, "/.crosspoint").clearCache();
    } else if (StringUtils::checkFileExtension(selectedFilePath, ".xtc") ||
               StringUtils::checkFileExtension(selectedFilePath, ".xtch")) {
      Xtc(selectedFilePath, "/.crosspoint").clearCache();
    } else if (StringUtils::checkFileExtension(selectedFilePath, ".txt") ||
               StringUtils::checkFileExtension(selectedFilePath, ".md")) {
      Txt txt(selectedFilePath, "/.crosspoint");
      Storage.removeDir(txt.getCachePath().c_str());
    }
  }

  const bool deleted = Storage.remove(selectedFilePath.c_str());
  if (deleted) {
    selectedFilePath.clear();
  }
  return deleted;
}

void MyLibraryActivity::onEnter() {
  Activity::onEnter();

  loadFiles();
  selectorIndex = 0;

  requestUpdate();
}

void MyLibraryActivity::onExit() {
  Activity::onExit();
  files.clear();
}

void MyLibraryActivity::loop() {
  if (mode == Mode::BMP_VIEW) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      mode = Mode::BROWSE;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !selectedFilePath.empty()) {
      enterFileActions(selectedFilePath);
      return;
    }
    return;
  }

  if (mode == Mode::FILE_ACTIONS) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      mode = Mode::BROWSE;
      requestUpdate();
      return;
    }

    buttonNavigator.onNextRelease([this] {
      fileActionIndex = ButtonNavigator::nextIndex(fileActionIndex, FILE_ACTION_COUNT);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this] {
      fileActionIndex = ButtonNavigator::previousIndex(fileActionIndex, FILE_ACTION_COUNT);
      requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      switch (fileActionIndex) {
        case 0:
          if (isBmpFile(selectedFilePath)) {
            mode = Mode::BMP_VIEW;
          } else {
            onSelectBook(selectedFilePath);
            return;
          }
          break;
        case 1:
          enterFileMoveBrowser();
          break;
        case 2:
          deleteSelectedFile();
          mode = Mode::BROWSE;
          loadFiles();
          selectorIndex = 0;
          break;
        default:
          mode = Mode::BROWSE;
          break;
      }
      requestUpdate();
    }
    return;
  }

  if (mode == Mode::FILE_MOVE_BROWSER) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (moveBrowserPath != "/") {
        moveBrowserPath = getParentPath(moveBrowserPath);
        fileMoveIndex = 0;
        loadMoveBrowseEntries();
      } else {
        mode = Mode::FILE_ACTIONS;
      }
      requestUpdate();
      return;
    }

    const int targetCount = static_cast<int>(moveBrowseEntries.size());
    if (targetCount <= 0) {
      mode = Mode::FILE_ACTIONS;
      requestUpdate();
      return;
    }

    buttonNavigator.onNextRelease([this, targetCount] {
      fileMoveIndex = ButtonNavigator::nextIndex(fileMoveIndex, targetCount);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, targetCount] {
      fileMoveIndex = ButtonNavigator::previousIndex(fileMoveIndex, targetCount);
      requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      const auto& entry = moveBrowseEntries[fileMoveIndex];
      if (entry.isMoveHere) {
        if (moveSelectedFileTo(entry.path)) {
          mode = Mode::BROWSE;
          loadFiles();
          selectorIndex = 0;
        } else {
          // Keep browsing destination after a failed move (existing file/same path).
          loadMoveBrowseEntries();
        }
      } else if (entry.isParent) {
        moveBrowserPath = entry.path;
        fileMoveIndex = 0;
        loadMoveBrowseEntries();
      } else {
        moveBrowserPath = entry.path;
        fileMoveIndex = 0;
        loadMoveBrowseEntries();
      }
      requestUpdate();
    }
    return;
  }

  // Long press BACK (1s+) goes to root folder
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS &&
      basepath != "/") {
    basepath = "/";
    loadFiles();
    selectorIndex = 0;
    return;
  }

  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (files.empty()) {
      return;
    }

    const std::string selectedEntry = files[selectorIndex];
    const std::string selectedPath = makeAbsolutePath(selectedEntry);

    if (selectedEntry.back() == '/') {
      basepath = selectedPath.substr(0, selectedPath.length() - 1);
      loadFiles();
      selectorIndex = 0;
      requestUpdate();
    } else if (isManagedFile(selectedEntry)) {
      if (mappedInput.getHeldTime() >= GO_HOME_MS) {
        enterFileActions(selectedPath);
      } else if (isBmpFile(selectedEntry)) {
        enterBmpView(selectedPath);
      } else {
        onSelectBook(selectedPath);
      }
    } else {
      onSelectBook(selectedPath);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);

        requestUpdate();
      } else {
        onGoHome();
      }
    }
  }

  int listSize = static_cast<int>(files.size());

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void MyLibraryActivity::render(Activity::RenderLock&&) {
  if (mode == Mode::BMP_VIEW) {
    renderBmpView();
    return;
  }

  if (mode == Mode::FILE_ACTIONS) {
    renderFileActions();
    return;
  }

  if (mode == Mode::FILE_MOVE_BROWSER) {
    renderFileMoveBrowser();
    return;
  }

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  std::string folderName = (basepath == "/") ? tr(STR_SD_CARD) : basepath.substr(basepath.rfind('/') + 1);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (files.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_BOOKS_FOUND));
  } else {
    const int pathY = contentTop;
    const int pathWidth = pageWidth - metrics.contentSidePadding * 2;
    const std::string pathLabel = renderer.truncatedText(SMALL_FONT_ID, basepath.c_str(), pathWidth);
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, pathLabel.c_str());

    const int listTop = pathY + renderer.getLineHeight(SMALL_FONT_ID) + 2;
    const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int rowGap = 1;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int rowPadY = 2;
    const int oneLineRowHeight = lineHeight + rowPadY * 2;
    const int listX = 0;
    const int listW = pageWidth;
    const int textX = listX + metrics.contentSidePadding;
    const int textW = listW - metrics.contentSidePadding * 2 - 3;

    std::vector<std::vector<std::string>> wrappedRows(files.size());
    std::vector<int> rowHeights(files.size(), oneLineRowHeight);
    for (size_t i = 0; i < files.size(); i++) {
      const std::string& name = files[i];
      std::string rowText = getDisplayNameForEntry(i);

      if (!name.empty() && name.back() != '/' && isBookFile(name)) {
        const std::string fullPath = makeAbsolutePath(name);
        auto cached = progressPrefixCache.find(fullPath);
        if (cached == progressPrefixCache.end()) {
          cached = progressPrefixCache.emplace(fullPath, BookProgress::getPrefix(fullPath)).first;
        }
        std::string progressPrefix = cached->second.empty() ? "-" : cached->second;
        if (progressPrefix == u8"―" || progressPrefix == "-") {
          progressPrefix = "[ ]";
        }
        rowText = progressPrefix + " " + name;
      }

      wrappedRows[i] = wrapTextToWidth(renderer, UI_10_FONT_ID, rowText, textW);
      rowHeights[i] = static_cast<int>(wrappedRows[i].size()) * lineHeight + rowPadY * 2;
    }

    if (selectorIndex >= files.size()) {
      selectorIndex = files.empty() ? 0 : files.size() - 1;
    }
    const int selected = static_cast<int>(selectorIndex);

    int startIndex = selected;
    int usedHeight = 0;
    while (startIndex >= 0) {
      const int blockHeight = rowHeights[static_cast<size_t>(startIndex)] + (usedHeight > 0 ? rowGap : 0);
      if (usedHeight + blockHeight > listHeight) break;
      usedHeight += blockHeight;
      startIndex--;
    }
    startIndex = std::max(0, startIndex + 1);

    int y = listTop;
    for (int i = startIndex; i < static_cast<int>(files.size()); i++) {
      const int rowHeight = rowHeights[static_cast<size_t>(i)];
      if (y + rowHeight > listTop + listHeight) break;

      const bool isSelected = i == selected;
      if (isSelected) {
        renderer.fillRect(listX, y, listW, rowHeight, true);
      }

      int lineY = y + rowPadY;
      for (const auto& line : wrappedRows[static_cast<size_t>(i)]) {
        renderer.drawText(UI_10_FONT_ID, textX, lineY, line.c_str(), !isSelected);
        lineY += lineHeight;
      }
      y += rowHeight + rowGap;
    }
  }

  // Help text
  const bool hasSelectedFile = !files.empty() && selectorIndex < files.size() && isManagedFile(files[selectorIndex]);
  const bool hasSelectedBmp = !files.empty() && selectorIndex < files.size() && isBmpFile(files[selectorIndex]);
  const auto labels = mappedInput.mapLabels(basepath == "/" ? tr(STR_HOME) : tr(STR_BACK),
                                            hasSelectedFile ? (hasSelectedBmp ? "View/Menu" : "Open/Menu")
                                                            : tr(STR_OPEN),
                                            tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void MyLibraryActivity::renderBmpView() {
  renderer.clearScreen();

  FsFile file;
  if (!Storage.openFileForRead("LIB", selectedFilePath, file)) {
    renderer.drawCenteredText(UI_12_FONT_ID, 80, "Failed to open BMP");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    renderer.drawCenteredText(UI_12_FONT_ID, 80, "Invalid BMP");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  int x = 0;
  int y = 0;

  if (bitmap.getWidth() > screenW || bitmap.getHeight() > screenH) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(screenW) / static_cast<float>(screenH);
    if (ratio > screenRatio) {
      y = std::round((static_cast<float>(screenH) - static_cast<float>(screenW) / ratio) / 2.0f);
    } else {
      x = std::round((static_cast<float>(screenW) - static_cast<float>(screenH) * ratio) / 2.0f);
    }
  } else {
    x = (screenW - bitmap.getWidth()) / 2;
    y = (screenH - bitmap.getHeight()) / 2;
  }

  renderer.drawBitmap(bitmap, x, y, screenW, screenH, 0.0f, 0.0f);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Actions", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void MyLibraryActivity::renderFileActions() {
  renderer.clearScreen();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int popupX = 24;
  const int popupY = 32;
  const int popupW = screenW - popupX * 2;
  const int popupH = screenH - popupY * 2;

  renderer.drawRect(popupX, popupY, popupW, popupH, true);
  renderer.drawCenteredText(UI_12_FONT_ID, popupY + 10, "File Actions", true, EpdFontFamily::REGULAR);

  const int rowStartY = popupY + 34;
  const int rowH = 26;
  for (int i = 0; i < FILE_ACTION_COUNT; i++) {
    const int rowY = rowStartY + i * rowH;
    const bool selected = (i == fileActionIndex);
    if (selected) {
      renderer.fillRect(popupX + 8, rowY - 2, popupW - 16, rowH, true);
    }
    const char* label = FILE_ACTION_LABELS[i];
    if (i == 0) {
      label = isBmpFile(selectedFilePath) ? "Open Image" : "Open Book";
    }
    renderer.drawText(UI_10_FONT_ID, popupX + 14, rowY, label, !selected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void MyLibraryActivity::renderFileMoveBrowser() {
  renderer.clearScreen();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int popupX = 16;
  const int popupY = 24;
  const int popupW = screenW - popupX * 2;
  const int popupH = screenH - popupY * 2;

  renderer.drawRect(popupX, popupY, popupW, popupH, true);
  std::string title = "Move To: " + ((moveBrowserPath == "/") ? std::string("/") : getBasename(moveBrowserPath));
  title = renderer.truncatedText(UI_12_FONT_ID, title.c_str(), popupW - 24);
  renderer.drawCenteredText(UI_12_FONT_ID, popupY + 10, title.c_str(), true, EpdFontFamily::REGULAR);

  const int maxRows = 8;
  const int rowStartY = popupY + 34;
  const int rowH = 24;
  int startIndex = 0;
  if (fileMoveIndex >= maxRows) {
    startIndex = fileMoveIndex - maxRows + 1;
  }

  for (int row = 0; row < maxRows; row++) {
    const int idx = startIndex + row;
    if (idx >= static_cast<int>(moveBrowseEntries.size())) break;
    const int rowY = rowStartY + row * rowH;
    const bool selected = (idx == fileMoveIndex);
    if (selected) {
      renderer.fillRect(popupX + 8, rowY - 2, popupW - 16, rowH, true);
    }
    std::string label = renderer.truncatedText(UI_10_FONT_ID, moveBrowseEntries[idx].name.c_str(), popupW - 26);
    renderer.drawText(UI_10_FONT_ID, popupX + 13, rowY, label.c_str(), !selected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

size_t MyLibraryActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}
