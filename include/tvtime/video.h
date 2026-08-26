#pragma once

#include <string>

namespace tvtime {

struct Video {
  std::string id;
  std::string title;
  std::string genre;
  int durationMinutes = 0;
  std::string source;
  std::string uri;
};

}  // namespace tvtime
