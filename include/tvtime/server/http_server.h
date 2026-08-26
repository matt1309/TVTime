#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "tvtime/media_library.h"

namespace tvtime::server {

class HttpServer {
 public:
  HttpServer(std::filesystem::path documentRoot, std::shared_ptr<MediaLibrary> library);

  void listen(const std::string& host, int port);

 private:
  std::filesystem::path documentRoot_;
  std::shared_ptr<MediaLibrary> library_;
};

}  // namespace tvtime::server
