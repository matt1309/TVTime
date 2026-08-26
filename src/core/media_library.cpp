#include "tvtime/media_library.h"

#include <algorithm>

namespace tvtime {

void MediaLibrary::addSource(std::shared_ptr<SourcePlugin> source) {
  sources_.push_back(std::move(source));
}

void MediaLibrary::addVideo(Video video) {
  videos_.push_back(std::move(video));
}

void MediaLibrary::importFromSources() {
  for (const auto& source : sources_) {
    auto discovered = source->discover();
    videos_.insert(
        videos_.end(),
        std::make_move_iterator(discovered.begin()),
        std::make_move_iterator(discovered.end()));
  }
}

const std::vector<Video>& MediaLibrary::videos() const {
  return videos_;
}

std::vector<std::string> MediaLibrary::sourceNames() const {
  std::vector<std::string> names;
  names.reserve(sources_.size());

  for (const auto& source : sources_) {
    names.push_back(source->name());
  }

  return names;
}

std::optional<Video> MediaLibrary::findVideo(const std::string& id) const {
  const auto match = std::find_if(
      videos_.begin(),
      videos_.end(),
      [&id](const Video& video) { return video.id == id; });

  if (match == videos_.end()) {
    return std::nullopt;
  }

  return *match;
}

}  // namespace tvtime
