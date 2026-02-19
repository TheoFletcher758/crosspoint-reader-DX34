#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

class MyLibraryActivity final : public Activity {
 private:
  struct MoveTarget {
    std::string label;
    std::string path;
  };

  enum class Mode { BROWSE, BMP_VIEW, FILE_ACTIONS, FILE_MOVE_TARGET };

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;
  Mode mode = Mode::BROWSE;
  int fileActionIndex = 0;
  int fileMoveTargetIndex = 0;
  std::string selectedFilePath;
  std::vector<MoveTarget> fileMoveTargets;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unordered_map<std::string, std::string> progressPrefixCache;

  // Callbacks
  const std::function<void(const std::string& path)> onSelectBook;
  const std::function<void()> onGoHome;

  // Data loading
  void loadFiles();
  std::string getDisplayNameForEntry(size_t index);
  std::string makeAbsolutePath(const std::string& name) const;
  static std::string getBasename(const std::string& path);
  static bool isBookFile(const std::string& filename);
  static bool isBmpFile(const std::string& filename);
  static bool isManagedFile(const std::string& filename);
  void enterBmpView(const std::string& bmpPath);
  void enterFileActions(const std::string& filePath);
  void buildFileMoveTargets();
  bool copyFile(const std::string& srcPath, const std::string& dstPath) const;
  bool moveSelectedFileTo(const std::string& targetDir);
  bool deleteSelectedFile();
  void renderBmpView();
  void renderFileActions();
  void renderFileMoveTargetPicker();
  size_t findEntry(const std::string& name) const;

 public:
  explicit MyLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::function<void()>& onGoHome,
                             const std::function<void(const std::string& path)>& onSelectBook,
                             std::string initialPath = "/")
      : Activity("MyLibrary", renderer, mappedInput),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)),
        onSelectBook(onSelectBook),
        onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
