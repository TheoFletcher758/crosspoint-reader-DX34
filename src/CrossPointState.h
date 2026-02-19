#pragma once
#include <iosfwd>
#include <string>
#include <vector>

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  std::string openEpubPath;
  uint8_t lastSleepImage;
  std::vector<std::string> sleepImagePlaylist;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()
