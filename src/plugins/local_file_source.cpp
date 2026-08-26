#include "tvtime/plugins/local_file_source.h"

#include <algorithm>
#include <cctype>

namespace tvtime {

namespace {

bool isSupportedVideoFile(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  return extension == ".mp4" || extension == ".mkv" || extension == ".avi" ||
         extension == ".mov" || extension == ".webm";
}

std::string makeId(const std::filesystem::path& path) {
  return "local:" + path.generic_string();
}

}  // namespace

LocalFileSource::LocalFileSource(std::filesystem::path root)
    : root_(std::move(root)) {}

std::string LocalFileSource::name() const {
  return "local-files";
}

std::vector<Video> LocalFileSource::discover() {
  std::vector<Video> videos;

  if (!std::filesystem::exists(root_)) {
    return videos;
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(root_)) {
    if (!entry.is_regular_file() || !isSupportedVideoFile(entry.path())) {
      continue;
    }

    videos.push_back(Video{
        makeId(entry.path()),
        entry.path().stem().string(),
        "unknown",
        0,
        name(),
        entry.path().string(),
    });
  }

  return videos;
}

}  // namespace tvtime
