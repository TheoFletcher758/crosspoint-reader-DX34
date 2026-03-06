#pragma once
#include <iosfwd>
#include <string>
#include <vector>

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  // Collections larger than this threshold are handled without a full in-memory
  // playlist; only the last-shown filename is persisted in that case.
  static constexpr size_t SLEEP_PLAYLIST_MAX_PERSIST = 200;

  std::string openEpubPath;
  uint8_t lastSleepImage = 0;
  std::vector<std::string> sleepImagePlaylist;
  std::string lastShownSleepFilename;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();

 private:
  bool loadFromBinaryFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()
