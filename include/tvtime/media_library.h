#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include "tvtime/source_plugin.h"
#include "tvtime/video.h"

namespace tvtime {

class MediaLibrary {
 public:
  void addSource(std::shared_ptr<SourcePlugin> source);
  void addVideo(Video video);
  void importFromSources();

  [[nodiscard]] std::vector<Video> videos() const;
  [[nodiscard]] std::vector<std::string> sourceNames() const;
  [[nodiscard]] std::optional<Video> findVideo(const std::string& id) const;

 private:
  std::mutex importMutex_;
  mutable std::shared_mutex mutex_;
  std::vector<Video> videos_;
  std::vector<std::shared_ptr<SourcePlugin>> sources_;
};

}  // namespace tvtime
