#include "RecentBooksStore.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>
#include <Xtc.h>

#include <algorithm>
#include <cctype>
#include <unordered_set>

#include "util/StringUtils.h"

namespace {
constexpr uint8_t RECENT_BOOKS_FILE_VERSION = 3;
constexpr char RECENT_BOOKS_FILE[] = "/.crosspoint/recent_v2.bin";
constexpr int MAX_RECENT_BOOKS = 100;

std::string normalizeRecentPath(std::string path) {
  if (path.empty()) {
    return path;
  }

  for (char &c : path) {
    if (c == '\\') {
      c = '/';
    }
  }

  std::string normalized = FsHelpers::normalisePath(path);
  if (normalized.empty()) {
    return "/";
  }
  if (normalized.front() != '/') {
    normalized.insert(normalized.begin(), '/');
  }
  while (normalized.size() > 1 && normalized.back() == '/') {
    normalized.pop_back();
  }
  return normalized;
}

std::string makeRecentPathKey(const std::string &rawPath) {
  std::string key = normalizeRecentPath(rawPath);
  for (char &c : key) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return key;
}

void dedupeRecentBooks(std::vector<RecentBook> &books) {
  std::vector<RecentBook> unique;
  unique.reserve(books.size());
  std::unordered_set<std::string> seen;
  seen.reserve(books.size());

  for (const auto &book : books) {
    const std::string normalizedPath = normalizeRecentPath(book.path);
    if (normalizedPath.empty()) {
      continue;
    }

    const std::string key = makeRecentPathKey(normalizedPath);
    if (seen.insert(key).second) {
      RecentBook copy = book;
      copy.path = normalizedPath;
      unique.push_back(std::move(copy));
    }
  }

  books.swap(unique);
  if (books.size() > MAX_RECENT_BOOKS) {
    books.resize(MAX_RECENT_BOOKS);
  }
}
} // namespace

RecentBooksStore RecentBooksStore::instance;

void RecentBooksStore::addBook(const std::string &path,
                               const std::string &title,
                               const std::string &author,
                               const std::string &coverBmpPath) {
  const std::string normalizedPath = normalizeRecentPath(path);
  const std::string normalizedKey = makeRecentPathKey(normalizedPath);
  if (normalizedPath.empty()) {
    return;
  }

  // Remove existing entry if present
  for (auto it = recentBooks.begin(); it != recentBooks.end();) {
    if (makeRecentPathKey(it->path) == normalizedKey) {
      it = recentBooks.erase(it);
    } else {
      ++it;
    }
  }

  // Add to front
  recentBooks.insert(recentBooks.begin(),
                     {normalizedPath, title, author, coverBmpPath});

  dedupeRecentBooks(recentBooks);

  saveToFile();
}

void RecentBooksStore::updateBook(const std::string &path,
                                  const std::string &title,
                                  const std::string &author,
                                  const std::string &coverBmpPath) {
  const std::string normalizedPath = normalizeRecentPath(path);
  const std::string normalizedKey = makeRecentPathKey(normalizedPath);
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook &book) {
                           return makeRecentPathKey(book.path) == normalizedKey;
                         });
  if (it != recentBooks.end()) {
    RecentBook &book = *it;
    book.path = normalizedPath;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    dedupeRecentBooks(recentBooks);
    saveToFile();
  }
}

void RecentBooksStore::removeBook(const std::string &path) {
  const std::string normalizedPath = normalizeRecentPath(path);
  const std::string normalizedKey = makeRecentPathKey(normalizedPath);
  bool removed = false;
  for (auto it = recentBooks.begin(); it != recentBooks.end();) {
    if (makeRecentPathKey(it->path) == normalizedKey) {
      it = recentBooks.erase(it);
      removed = true;
    } else {
      ++it;
    }
  }
  if (removed) {
    dedupeRecentBooks(recentBooks);
    saveToFile();
  }
}

bool RecentBooksStore::saveToFile() const {
  // Make sure the directory exists
  Storage.mkdir("/.crosspoint");

  const char *tmpFile = "/.crosspoint/recent_tmp.bin";
  if (Storage.exists(tmpFile)) {
    Storage.remove(tmpFile);
  }

  FsFile outputFile;
  if (!Storage.openFileForWrite("RBS", tmpFile, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, RECENT_BOOKS_FILE_VERSION);
  const uint8_t count = static_cast<uint8_t>(recentBooks.size());
  serialization::writePod(outputFile, count);

  for (const auto &book : recentBooks) {
    serialization::writeString(outputFile, book.path);
    serialization::writeString(outputFile, book.title);
    serialization::writeString(outputFile, book.author);
    serialization::writeString(outputFile, book.coverBmpPath);
  }

  outputFile.sync();
  outputFile.close();

  // Atomically swap the temp file with the actual file
  if (Storage.exists(RECENT_BOOKS_FILE)) {
    Storage.remove(RECENT_BOOKS_FILE);
  }
  Storage.rename(tmpFile, RECENT_BOOKS_FILE);

  LOG_DBG("RBS", "Recent books saved to file (%d entries)", count);
  return true;
}

RecentBook RecentBooksStore::getDataFromBook(std::string path) const {
  std::string lastBookFileName = "";
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    lastBookFileName = path.substr(lastSlash + 1);
  }

  LOG_DBG("RBS", "Loading recent book: %s", path.c_str());

  // If epub, try to load the metadata for title/author and cover
  if (StringUtils::checkFileExtension(lastBookFileName, ".epub")) {
    Epub epub(path, "/.crosspoint");
    epub.load(false, true);
    return RecentBook{path, epub.getTitle(), epub.getAuthor(),
                      epub.getThumbBmpPath()};
  } else if (StringUtils::checkFileExtension(lastBookFileName, ".xtch") ||
             StringUtils::checkFileExtension(lastBookFileName, ".xtc")) {
    // Handle XTC file
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      return RecentBook{path, xtc.getTitle(), xtc.getAuthor(),
                        xtc.getThumbBmpPath()};
    }
  } else if (StringUtils::checkFileExtension(lastBookFileName, ".txt") ||
             StringUtils::checkFileExtension(lastBookFileName, ".md")) {
    return RecentBook{path, lastBookFileName, "", ""};
  }
  return RecentBook{path, "", "", ""};
}

bool RecentBooksStore::loadFromFile() {
  FsFile inputFile;
  bool usingLegacy = false;

  if (!Storage.exists(RECENT_BOOKS_FILE) &&
      Storage.exists("/.crosspoint/recent.bin")) {
    usingLegacy = true;
    LOG_DBG("RBS", "using legacy recent.bin for migration");
  }

  const char *fileToOpen =
      usingLegacy ? "/.crosspoint/recent.bin" : RECENT_BOOKS_FILE;

  if (!Storage.openFileForRead("RBS", fileToOpen, inputFile)) {
    if (usingLegacy) {
      Storage.remove("/.crosspoint/recent.bin");
    }
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version != RECENT_BOOKS_FILE_VERSION) {
    if (version == 1 || version == 2) {
      // Old version, just read paths
      uint8_t count;
      serialization::readPod(inputFile, count);
      recentBooks.clear();
      recentBooks.reserve(count);
      for (uint8_t i = 0; i < count; i++) {
        std::string path;
        serialization::readString(inputFile, path);

        // load book to get missing data
        RecentBook book = getDataFromBook(path);
        if (book.title.empty() && book.author.empty() && version == 2) {
          // Fall back to loading what we can from the store
          std::string title, author;
          serialization::readString(inputFile, title);
          serialization::readString(inputFile, author);
          recentBooks.push_back({path, title, author, ""});
        } else {
          recentBooks.push_back(book);
        }
      }
    } else {
      LOG_ERR("RBS", "Deserialization failed: Unknown version %u", version);
      inputFile.close();
      if (usingLegacy) {
        Storage.remove("/.crosspoint/recent.bin");
      }
      return false;
    }
  } else {
    uint8_t count;
    serialization::readPod(inputFile, count);

    recentBooks.clear();
    recentBooks.reserve(count);

    for (uint8_t i = 0; i < count; i++) {
      std::string path, title, author, coverBmpPath;
      serialization::readString(inputFile, path);
      serialization::readString(inputFile, title);
      serialization::readString(inputFile, author);
      serialization::readString(inputFile, coverBmpPath);
      recentBooks.push_back(
          {normalizeRecentPath(path), title, author, coverBmpPath});
    }
  }

  dedupeRecentBooks(recentBooks);
  inputFile.close();

  if (Storage.exists("/.crosspoint/recent.bin")) {
    LOG_DBG("RBS", "Cleaning up legacy recent.bin");
    Storage.remove("/.crosspoint/recent.bin");
    if (usingLegacy) {
      saveToFile();
    }
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries)",
          recentBooks.size());
  return true;
}
