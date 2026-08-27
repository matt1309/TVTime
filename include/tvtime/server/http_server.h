#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "tvtime/media_library.h"
#include "tvtime/schedule.h"

namespace tvtime::server {

class HttpServer {
 public:
  // `scheduleFile` is optional persistence for the schedule; when empty the
  // schedule only lives in memory for the lifetime of the process.
  HttpServer(
      std::filesystem::path documentRoot,
      std::shared_ptr<MediaLibrary> library,
      std::shared_ptr<Schedule> schedule = std::make_shared<Schedule>(),
      std::filesystem::path scheduleFile = {});

  void listen(const std::string& host, int port);

 private:
  std::filesystem::path documentRoot_;
  std::shared_ptr<MediaLibrary> library_;
  std::shared_ptr<Schedule> schedule_;
  std::filesystem::path scheduleFile_;
};

}  // namespace tvtime::server
