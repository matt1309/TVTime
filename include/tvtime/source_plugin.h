#pragma once

#include <string>
#include <vector>

#include "tvtime/video.h"

namespace tvtime {

class SourcePlugin {
 public:
  virtual ~SourcePlugin() = default;

  [[nodiscard]] virtual std::string name() const = 0;
  [[nodiscard]] virtual std::vector<Video> discover() = 0;
};

}  // namespace tvtime
