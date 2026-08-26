#pragma once

#include <filesystem>
#include <string>

#include "tvtime/media_library.h"

namespace tvtime::server {

class HttpServer {
 public:
  HttpServer(std::filesystem::path documentRoot, MediaLibrary& library);

  void listen(const std::string& host, int port);

 private:
  std::filesystem::path documentRoot_;
  MediaLibrary& library_;
};

}  // namespace tvtime::server
