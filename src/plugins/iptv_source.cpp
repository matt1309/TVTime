#include "tvtime/plugins/iptv_source.h"

#include <fstream>
#include <sstream>

namespace tvtime {

namespace {

std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

// Extracts the value of a quoted attribute such as group-title="News" from an
// #EXTINF line. Returns an empty string when the attribute is missing.
std::string extractAttribute(const std::string& line, const std::string& key) {
  const auto keyPos = line.find(key + "=\"");
  if (keyPos == std::string::npos) {
    return "";
  }

  const auto valueStart = keyPos + key.size() + 2;
  const auto valueEnd = line.find('"', valueStart);
  if (valueEnd == std::string::npos) {
    return "";
  }

  return line.substr(valueStart, valueEnd - valueStart);
}

// The display title of an #EXTINF entry is the text following the last comma
// on the line, e.g. `#EXTINF:-1 group-title="News",BBC News HD`.
std::string extractTitle(const std::string& line) {
  const auto commaPos = line.find_last_of(',');
  if (commaPos == std::string::npos || commaPos + 1 >= line.size()) {
    return "";
  }

  return trim(line.substr(commaPos + 1));
}

std::string makeId(const std::string& url) {
  return "iptv:" + url;
}

}  // namespace

IptvSource::IptvSource(std::filesystem::path playlistPath)
    : playlistPath_(std::move(playlistPath)) {}

std::string IptvSource::name() const {
  return "iptv";
}

std::vector<Video> IptvSource::discover() {
  std::vector<Video> videos;

  std::error_code error;
  if (!std::filesystem::exists(playlistPath_, error) || error) {
    return videos;
  }

  std::ifstream file(playlistPath_);
  if (!file.is_open()) {
    return videos;
  }

  std::string pendingTitle;
  std::string pendingGenre;
  std::string line;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty()) {
      continue;
    }

    if (line.rfind("#EXTINF:", 0) == 0) {
      pendingTitle = extractTitle(line);
      pendingGenre = extractAttribute(line, "group-title");
      continue;
    }

    if (line.front() == '#') {
      // Other directives (#EXTM3U, #EXTGRP, #EXTVLCOPT, ...) are ignored.
      continue;
    }

    // Any non-comment, non-empty line is treated as a stream URL.
    const std::string title = pendingTitle.empty() ? line : pendingTitle;
    const std::string genre = pendingGenre.empty() ? "iptv" : pendingGenre;

    videos.push_back(Video{
        makeId(line),
        title,
        genre,
        0,
        name(),
        line,
    });

    pendingTitle.clear();
    pendingGenre.clear();
  }

  return videos;
}

}  // namespace tvtime
