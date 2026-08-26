#pragma once

#include <memory>
#include <optional>
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

  [[nodiscard]] const std::vector<Video>& videos() const;
  [[nodiscard]] std::vector<std::string> sourceNames() const;
  [[nodiscard]] std::optional<Video> findVideo(const std::string& id) const;

 private:
  std::vector<Video> videos_;
  std::vector<std::shared_ptr<SourcePlugin>> sources_;
};

}  // namespace tvtime
