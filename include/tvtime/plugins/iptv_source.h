#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "tvtime/source_plugin.h"

namespace tvtime {

// Reads an extended M3U playlist (the format used by most IPTV providers)
// and exposes each entry as a live "video" whose URI is the stream URL.
class IptvSource : public SourcePlugin {
 public:
  explicit IptvSource(std::filesystem::path playlistPath);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::vector<Video> discover() override;

 private:
  std::filesystem::path playlistPath_;
};

}  // namespace tvtime
