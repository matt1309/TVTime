#include "tvtime/plugins/local_file_source.h"

#include <algorithm>
#include <cctype>
#include <system_error>

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

  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      root_,
      std::filesystem::directory_options::skip_permission_denied,
      error);
  const std::filesystem::recursive_directory_iterator end;

  while (!error && iterator != end) {
    const auto& entry = *iterator;
    if (!entry.is_regular_file(error) && error) {
      error.clear();
      iterator.increment(error);
      continue;
    }

    if (!entry.is_regular_file(error) || !isSupportedVideoFile(entry.path())) {
      error.clear();
      iterator.increment(error);
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

    iterator.increment(error);
  }

  return videos;
}

}  // namespace tvtime
