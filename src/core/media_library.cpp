#include "tvtime/media_library.h"

#include <algorithm>
#include <mutex>

namespace tvtime {

void MediaLibrary::addSource(std::shared_ptr<SourcePlugin> source) {
  std::unique_lock lock(mutex_);
  sources_.push_back(std::move(source));
}

void MediaLibrary::addVideo(Video video) {
  std::unique_lock lock(mutex_);
  videos_.push_back(std::move(video));
}

void MediaLibrary::importFromSources() {
  std::vector<std::shared_ptr<SourcePlugin>> sources;
  {
    std::shared_lock lock(mutex_);
    sources = sources_;
  }

  for (const auto& source : sources) {
    auto discovered = source->discover();
    std::unique_lock lock(mutex_);
    videos_.insert(
        videos_.end(),
        std::make_move_iterator(discovered.begin()),
        std::make_move_iterator(discovered.end()));
  }
}

std::vector<Video> MediaLibrary::videos() const {
  std::shared_lock lock(mutex_);
  return videos_;
}

std::vector<std::string> MediaLibrary::sourceNames() const {
  std::shared_lock lock(mutex_);
  std::vector<std::string> names;
  names.reserve(sources_.size());

  for (const auto& source : sources_) {
    names.push_back(source->name());
  }

  return names;
}

std::optional<Video> MediaLibrary::findVideo(const std::string& id) const {
  std::shared_lock lock(mutex_);
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
