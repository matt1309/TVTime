#pragma once

#include <string>
#include <vector>

#include "tvtime/source_plugin.h"

namespace tvtime {

class DlnaSource final : public SourcePlugin {
 public:
  DlnaSource();

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::vector<Video> discover() override;

 private:
  std::vector<std::string> discoverDevices();
  std::vector<Video> browseDevice(const std::string& deviceUrl);
};

}  // namespace tvtime
