#include <cstdlib>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "tvtime/media_library.h"
#include "tvtime/plugins/local_file_source.h"
#include "tvtime/plugins/dlna_source.h"
#include "tvtime/server/http_server.h"

namespace {

int parsePort(const char* value) {
  if (value == nullptr) {
    return 8080;
  }

  errno = 0;
  char* end = nullptr;
  const auto port = std::strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || port < 1 ||
      port > std::numeric_limits<uint16_t>::max()) {
    throw std::invalid_argument("TVTIME_PORT must be an integer from 1 to 65535");
  }

  return static_cast<int>(port);
}

std::string parseHost(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return "127.0.0.1";
  }

  return value;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const std::filesystem::path documentRoot =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
    const std::filesystem::path mediaRoot =
        argc > 2 ? std::filesystem::path(argv[2]) : documentRoot / "media";

    auto library = std::make_shared<tvtime::MediaLibrary>();
    library->addSource(std::make_shared<tvtime::LocalFileSource>(mediaRoot));
    library->addSource(std::make_shared<tvtime::DlnaSource>());
    library->importFromSources();

    tvtime::server::HttpServer server(documentRoot, library);
    server.listen(
        parseHost(std::getenv("TVTIME_HOST")),
        parsePort(std::getenv("TVTIME_PORT")));
  } catch (const std::exception& error) {
    std::cerr << "TVTime failed to start: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
