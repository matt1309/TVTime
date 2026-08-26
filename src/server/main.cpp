#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "tvtime/media_library.h"
#include "tvtime/plugins/local_file_source.h"
#include "tvtime/server/http_server.h"

namespace {

int parsePort(const char* value) {
  if (value == nullptr) {
    return 8080;
  }

  return std::stoi(value);
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const std::filesystem::path documentRoot =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
    const std::filesystem::path mediaRoot =
        argc > 2 ? std::filesystem::path(argv[2]) : documentRoot / "media";

    tvtime::MediaLibrary library;
    library.addSource(std::make_shared<tvtime::LocalFileSource>(mediaRoot));
    library.importFromSources();

    tvtime::server::HttpServer server(documentRoot, library);
    server.listen("127.0.0.1", parsePort(std::getenv("TVTIME_PORT")));
  } catch (const std::exception& error) {
    std::cerr << "TVTime failed to start: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
