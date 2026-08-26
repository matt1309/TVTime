#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "tvtime/source_plugin.h"

namespace tvtime {

class LocalFileSource final : public SourcePlugin {
 public:
  explicit LocalFileSource(std::filesystem::path root);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::vector<Video> discover() override;

 private:
  std::filesystem::path root_;
};

}  // namespace tvtime
