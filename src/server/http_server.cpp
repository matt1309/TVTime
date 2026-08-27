#include "tvtime/server/http_server.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

namespace tvtime::server {

namespace {

constexpr std::size_t kMaxActiveClients = 64;

std::string jsonEscape(const std::string& value) {
  std::ostringstream escaped;

  for (const char ch : value) {
    switch (ch) {
      case '"':
        escaped << "\\\"";
        break;
      case '\\':
        escaped << "\\\\";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          std::ostringstream hex;
          hex << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(static_cast<unsigned char>(ch));
          escaped << "\\u" << hex.str();
        } else {
          escaped << ch;
        }
        break;
    }
  }

  return escaped.str();
}

std::string videosJson(const MediaLibrary& library) {
  std::ostringstream body;
  body << "[";

  const auto videos = library.videos();
  for (std::size_t index = 0; index < videos.size(); ++index) {
    const auto& video = videos[index];
    if (index != 0) {
      body << ",";
    }

    body << "{\"id\":\"" << jsonEscape(video.id) << "\","
         << "\"title\":\"" << jsonEscape(video.title) << "\","
         << "\"genre\":\"" << jsonEscape(video.genre) << "\","
         << "\"durationMinutes\":" << video.durationMinutes << ","
         << "\"source\":\"" << jsonEscape(video.source) << "\","
         << "\"uri\":\"" << jsonEscape(video.uri) << "\"}";
  }

  body << "]";
  return body.str();
}

std::string sourcesJson(const MediaLibrary& library) {
  std::ostringstream body;
  body << "[";

  const auto sources = library.sourceNames();
  for (std::size_t index = 0; index < sources.size(); ++index) {
    if (index != 0) {
      body << ",";
    }

    body << "\"" << jsonEscape(sources[index]) << "\"";
  }

  body << "]";
  return body.str();
}

std::string contentType(const std::filesystem::path& path) {
  const auto extension = path.extension().string();

  if (extension == ".html") {
    return "text/html; charset=utf-8";
  }
  if (extension == ".css") {
    return "text/css; charset=utf-8";
  }
  if (extension == ".js") {
    return "application/javascript; charset=utf-8";
  }
  if (extension == ".json") {
    return "application/json; charset=utf-8";
  }

  return "application/octet-stream";
}

std::string response(
    int status,
    const std::string& reason,
    const std::string& type,
    const std::string& body,
    const std::string& extraHeaders = "") {
  std::ostringstream output;
  output << "HTTP/1.1 " << status << " " << reason << "\r\n"
         << "Content-Type: " << type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << extraHeaders
         << "Connection: close\r\n\r\n"
         << body;

  return output.str();
}

std::string decodePath(std::string path) {
  const auto query = path.find('?');
  if (query != std::string::npos) {
    path.erase(query);
  }

  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

bool isUnsafePath(const std::filesystem::path& path) {
  return path.is_absolute() ||
         std::any_of(
             path.begin(),
             path.end(),
             [](const auto& part) { return part == ".."; });
}

bool isInsideRoot(
    const std::filesystem::path& root,
    const std::filesystem::path& requestedPath) {
  std::error_code error;
  const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
  if (error) {
    return false;
  }

  const auto canonicalRequested = std::filesystem::weakly_canonical(requestedPath, error);
  if (error) {
    return false;
  }

  if (std::distance(canonicalRequested.begin(), canonicalRequested.end()) <
      std::distance(canonicalRoot.begin(), canonicalRoot.end())) {
    return false;
  }

  return std::mismatch(
             canonicalRoot.begin(),
             canonicalRoot.end(),
             canonicalRequested.begin(),
             canonicalRequested.end())
             .first == canonicalRoot.end();
}

std::optional<std::string> readFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::nullopt;
  }

  std::ostringstream body;
  body << file.rdbuf();
  if (file.bad()) {
    return std::nullopt;
  }

  return body.str();
}

std::string route(
    const std::filesystem::path& documentRoot,
    const MediaLibrary& library,
    const std::string& method,
    const std::string& target) {
  if (method != "GET") {
    return response(
        405,
        "Method Not Allowed",
        "text/plain; charset=utf-8",
        "Method not allowed",
        "Allow: GET\r\n");
  }

  if (target == "/api/health") {
    return response(200, "OK", "application/json; charset=utf-8", "{\"status\":\"ok\",\"service\":\"TVTime\"}");
  }

  if (target == "/api/videos") {
    return response(200, "OK", "application/json; charset=utf-8", videosJson(library));
  }

  if (target == "/api/sources") {
    return response(200, "OK", "application/json; charset=utf-8", sourcesJson(library));
  }

  if (target.rfind("/api/stream/", 0) == 0) {
    const std::string videoId = target.substr(12);
    if (videoId.empty()) {
      return response(400, "Bad Request", "text/plain; charset=utf-8", "Video ID required");
    }

    const auto videos = library.videos();
    const auto video = std::find_if(videos.begin(), videos.end(), [&videoId](const Video& v) {
      return v.id == videoId;
    });

    if (video == videos.end()) {
      return response(404, "Not Found", "text/plain; charset=utf-8", "Video not found");
    }

    if (video->uri.empty()) {
      return response(404, "Not Found", "text/plain; charset=utf-8", "No URI available for this video");
    }

    const std::filesystem::path videoPath(video->uri);
    std::error_code fileError;
    const bool exists = std::filesystem::exists(videoPath, fileError);
    if (fileError || !exists) {
      return response(404, "Not Found", "text/plain; charset=utf-8", "Video file not found");
    }

    const bool regularFile = std::filesystem::is_regular_file(videoPath, fileError);
    if (fileError || !regularFile) {
      return response(404, "Not Found", "text/plain; charset=utf-8", "Not a regular file");
    }

    const auto body = readFile(videoPath);
    if (!body.has_value()) {
      return response(500, "Internal Server Error", "text/plain; charset=utf-8", "Unable to read video file");
    }

    const auto extension = videoPath.extension().string();
    std::string mimeType = "application/octet-stream";
    if (extension == ".mp4") {
      mimeType = "video/mp4";
    } else if (extension == ".mkv") {
      mimeType = "video/x-matroska";
    } else if (extension == ".webm") {
      mimeType = "video/webm";
    } else if (extension == ".avi") {
      mimeType = "video/x-msvideo";
    } else if (extension == ".mov") {
      mimeType = "video/quicktime";
    }

    return response(200, "OK", mimeType, *body);
  }

  auto decoded = decodePath(target);
  if (decoded == "/") {
    decoded = "/index.html";
  }

  if (decoded.empty() || decoded.front() != '/') {
    return response(400, "Bad Request", "text/plain; charset=utf-8", "Bad request");
  }

  std::filesystem::path relative = decoded.substr(1);
  relative = relative.lexically_normal();

  if (isUnsafePath(relative)) {
    return response(400, "Bad Request", "text/plain; charset=utf-8", "Bad request");
  }

  const auto filePath = documentRoot / relative;
  if (!isInsideRoot(documentRoot, filePath)) {
    return response(400, "Bad Request", "text/plain; charset=utf-8", "Bad request");
  }

  std::error_code fileError;
  const bool exists = std::filesystem::exists(filePath, fileError);
  if (fileError || !exists) {
    return response(404, "Not Found", "text/plain; charset=utf-8", "Not found");
  }

  const bool regularFile = std::filesystem::is_regular_file(filePath, fileError);
  if (fileError || !regularFile) {
    return response(404, "Not Found", "text/plain; charset=utf-8", "Not found");
  }

  const auto body = readFile(filePath);
  if (!body.has_value()) {
    return response(500, "Internal Server Error", "text/plain; charset=utf-8", "Unable to read file");
  }

  return response(200, "OK", contentType(filePath), *body);
}

#ifndef _WIN32
class Socket {
 public:
  explicit Socket(int descriptor) : descriptor_(descriptor) {}
  ~Socket() {
    if (descriptor_ >= 0) {
      close(descriptor_);
    }
  }

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  [[nodiscard]] int descriptor() const {
    return descriptor_;
  }

 private:
  int descriptor_;
};

void sendResponse(int descriptor, const std::string& output) {
  std::size_t sent = 0;
  while (sent < output.size()) {
    const auto sentNow = send(descriptor, output.data() + sent, output.size() - sent, 0);
    if (sentNow <= 0) {
      break;
    }
    sent += static_cast<std::size_t>(sentNow);
  }
}

bool reserveClientSlot(const std::shared_ptr<std::atomic_size_t>& activeClients) {
  auto current = activeClients->load();
  while (current < kMaxActiveClients) {
    if (activeClients->compare_exchange_weak(current, current + 1)) {
      return true;
    }
  }

  return false;
}

void handleClient(
    int clientDescriptor,
    const std::filesystem::path& documentRoot,
    std::shared_ptr<const MediaLibrary> library) {
  const Socket client(clientDescriptor);

  std::string requestText;
  std::array<char, 4096> buffer{};
  auto headerEnd = std::string::npos;
  while (headerEnd == std::string::npos && requestText.size() < 16384) {
    const auto bytesRemaining = 16384 - requestText.size();
    const auto bytesToRead = std::min(buffer.size(), bytesRemaining);
    const auto previousSize = requestText.size();
    const auto bytesRead = recv(client.descriptor(), buffer.data(), bytesToRead, 0);
    if (bytesRead <= 0) {
      break;
    }

    requestText.append(buffer.data(), static_cast<std::size_t>(bytesRead));
    const auto searchFrom = previousSize > 3 ? previousSize - 3 : 0;
    headerEnd = requestText.find("\r\n\r\n", searchFrom);
  }

  if (headerEnd == std::string::npos) {
    const auto output = response(
        400,
        "Bad Request",
        "text/plain; charset=utf-8",
        "Incomplete or oversized request headers");
    send(client.descriptor(), output.data(), output.size(), 0);
    return;
  }

  std::istringstream request(requestText);
  std::string method;
  std::string target;
  request >> method >> target;

  const auto output = route(documentRoot, *library, method, target);
  sendResponse(client.descriptor(), output);
}

#endif

}  // namespace

HttpServer::HttpServer(std::filesystem::path documentRoot, std::shared_ptr<MediaLibrary> library)
    : documentRoot_(std::filesystem::absolute(std::move(documentRoot))),
      library_(std::move(library)) {}

void HttpServer::listen(const std::string& host, int port) {
#ifdef _WIN32
  (void)host;
  (void)port;
  throw std::runtime_error("The development HTTP server currently supports POSIX sockets only.");
#else
  const Socket serverSocket(socket(AF_INET, SOCK_STREAM, 0));
  if (serverSocket.descriptor() < 0) {
    throw std::runtime_error(std::string("socket failed: ") + std::strerror(errno));
  }

  int enabled = 1;
  if (setsockopt(serverSocket.descriptor(), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) <
      0) {
    std::cerr << "warning: SO_REUSEADDR failed: " << std::strerror(errno) << "\n";
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
    throw std::runtime_error("host must be an IPv4 address");
  }

  if (bind(serverSocket.descriptor(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    throw std::runtime_error(std::string("bind failed: ") + std::strerror(errno));
  }

  if (::listen(serverSocket.descriptor(), 16) < 0) {
    throw std::runtime_error(std::string("listen failed: ") + std::strerror(errno));
  }

  std::cout << "TVTime server listening on http://" << host << ":" << port << "\n";

  auto activeClients = std::make_shared<std::atomic_size_t>(0);
  while (true) {
    const auto clientDescriptor = accept(serverSocket.descriptor(), nullptr, nullptr);
    if (clientDescriptor < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }

      const auto message = std::string("accept failed: ") + std::strerror(errno);
      throw std::runtime_error(message);
    }

    if (!reserveClientSlot(activeClients)) {
      const Socket client(clientDescriptor);
      sendResponse(
          client.descriptor(),
          response(
              503,
              "Service Unavailable",
              "text/plain; charset=utf-8",
              "Too many active connections"));
      continue;
    }

    std::thread(
        [clientDescriptor, documentRoot = documentRoot_, library = library_, activeClients]() {
          handleClient(clientDescriptor, documentRoot, library);
          activeClients->fetch_sub(1);
        })
        .detach();
  }
#endif
}

}  // namespace tvtime::server
