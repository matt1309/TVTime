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
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
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

// Parses a single flat JSON object (no nested objects/arrays) into a map of
// raw string/number values. This is intentionally minimal -- just enough to
// read the small request bodies TVTime's own frontend sends -- rather than a
// general purpose JSON library.
std::optional<std::map<std::string, std::string>> parseFlatJsonObject(
    const std::string& body) {
  std::size_t index = 0;
  const auto skipSpace = [&]() {
    while (index < body.size() && std::isspace(static_cast<unsigned char>(body[index]))) {
      ++index;
    }
  };

  skipSpace();
  if (index >= body.size() || body[index] != '{') {
    return std::nullopt;
  }
  ++index;

  std::map<std::string, std::string> values;
  skipSpace();
  if (index < body.size() && body[index] == '}') {
    return values;
  }

  const auto parseString = [&]() -> std::optional<std::string> {
    if (index >= body.size() || body[index] != '"') {
      return std::nullopt;
    }
    ++index;
    std::string value;
    while (index < body.size() && body[index] != '"') {
      if (body[index] == '\\' && index + 1 < body.size()) {
        ++index;
        switch (body[index]) {
          case 'n':
            value += '\n';
            break;
          case 't':
            value += '\t';
            break;
          case 'r':
            value += '\r';
            break;
          case '"':
            value += '"';
            break;
          case '\\':
            value += '\\';
            break;
          default:
            value += body[index];
            break;
        }
      } else {
        value += body[index];
      }
      ++index;
    }

    if (index >= body.size()) {
      return std::nullopt;
    }
    ++index;
    return value;
  };

  while (true) {
    skipSpace();
    auto key = parseString();
    if (!key.has_value()) {
      return std::nullopt;
    }

    skipSpace();
    if (index >= body.size() || body[index] != ':') {
      return std::nullopt;
    }
    ++index;
    skipSpace();

    std::string value;
    if (index < body.size() && body[index] == '"') {
      auto stringValue = parseString();
      if (!stringValue.has_value()) {
        return std::nullopt;
      }
      value = std::move(*stringValue);
    } else {
      const auto start = index;
      while (index < body.size() && body[index] != ',' && body[index] != '}' &&
             !std::isspace(static_cast<unsigned char>(body[index]))) {
        ++index;
      }
      if (index == start) {
        return std::nullopt;
      }
      value = body.substr(start, index - start);
    }

    values[*key] = value;
    skipSpace();
    if (index < body.size() && body[index] == ',') {
      ++index;
      continue;
    }
    if (index < body.size() && body[index] == '}') {
      break;
    }
    return std::nullopt;
  }

  return values;
}

std::map<std::string, std::string> parseQuery(const std::string& query) {
  std::map<std::string, std::string> params;
  std::istringstream stream(query);
  std::string pair;
  while (std::getline(stream, pair, '&')) {
    const auto equals = pair.find('=');
    if (equals == std::string::npos) {
      continue;
    }
    params[pair.substr(0, equals)] = pair.substr(equals + 1);
  }
  return params;
}

std::string scheduleSlotJson(const ProgramSlot& slot) {
  std::ostringstream body;
  body << "{\"channel\":\"" << jsonEscape(slot.channel) << "\","
       << "\"videoId\":\"" << jsonEscape(slot.videoId) << "\","
       << "\"startMinute\":" << slot.startMinute << ","
       << "\"endMinute\":" << slot.endMinute << "}";
  return body.str();
}

std::string scheduleSlotsJson(const std::vector<ProgramSlot>& slots) {
  std::ostringstream body;
  body << "[";
  for (std::size_t index = 0; index < slots.size(); ++index) {
    if (index != 0) {
      body << ",";
    }
    body << scheduleSlotJson(slots[index]);
  }
  body << "]";
  return body.str();
}

std::string urlEncode(const std::string& value) {
  std::ostringstream encoded;
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      encoded << ch;
    } else {
      encoded << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(ch) << std::nouppercase << std::dec;
    }
  }
  return encoded.str();
}

std::string urlDecode(const std::string& value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const std::string hex = value.substr(i + 1, 2);
      char* end = nullptr;
      const long code = std::strtol(hex.c_str(), &end, 16);
      if (end == hex.c_str() + hex.size()) {
        decoded += static_cast<char>(code);
        i += 2;
        continue;
      }
    }
    decoded += value[i];
  }
  return decoded;
}

bool isRemoteUri(const std::string& uri) {
  return uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0;
}

// A stable identifier for the virtual tuner. Real HDHomeRun devices use an
// 8 hex digit hardware ID; any fixed value works for software emulation.
constexpr const char* kDeviceId = "TVT10001";
constexpr const char* kFriendlyName = "TVTime HDHomeRun";
constexpr int kTunerCount = 4;

std::string discoverJson(const std::string& baseUrl) {
  std::ostringstream body;
  body << "{"
       << "\"FriendlyName\":\"" << jsonEscape(kFriendlyName) << "\","
       << "\"ModelNumber\":\"HDTC-2US\","
       << "\"FirmwareName\":\"tvtime_tuner\","
       << "\"FirmwareVersion\":\"20240101\","
       << "\"DeviceID\":\"" << kDeviceId << "\","
       << "\"DeviceAuth\":\"tvtime\","
       << "\"TunerCount\":" << kTunerCount << ","
       << "\"BaseURL\":\"" << jsonEscape(baseUrl) << "\","
       << "\"LineupURL\":\"" << jsonEscape(baseUrl) << "/lineup.json\""
       << "}";
  return body.str();
}

std::string lineupStatusJson() {
  return "{\"ScanInProgress\":0,\"ScanPossible\":1,\"Source\":\"Cable\","
         "\"SourceList\":[\"Cable\"]}";
}

// Every discovered video acts as a tunable "channel" for DVR software such as
// Plex or Emby. IPTV playlist entries are the primary intended use case, but
// any indexed video (local file, DLNA, IPTV) can be tuned this way.
std::string lineupJson(const MediaLibrary& library, const std::string& baseUrl) {
  std::ostringstream body;
  body << "[";

  const auto videos = library.videos();
  int guideNumber = 1;
  bool first = true;
  for (const auto& video : videos) {
    if (video.uri.empty()) {
      continue;
    }

    if (!first) {
      body << ",";
    }
    first = false;

    body << "{\"GuideNumber\":\"" << guideNumber << "\","
         << "\"GuideName\":\"" << jsonEscape(video.title) << "\","
         << "\"URL\":\"" << jsonEscape(baseUrl) << "/api/stream/"
         << jsonEscape(urlEncode(video.id)) << "\"}";
    ++guideNumber;
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
    Schedule& schedule,
    const std::filesystem::path& scheduleFile,
    const std::string& method,
    const std::string& target,
    const std::string& hostHeader,
    const std::string& requestBody) {
  const std::string baseUrl = "http://" + (hostHeader.empty() ? "127.0.0.1:8080" : hostHeader);

  const auto path = decodePath(target);
  const auto queryStart = target.find('?');
  const auto query = parseQuery(
      queryStart == std::string::npos ? "" : target.substr(queryStart + 1));

  if (path == "/api/schedule" && method == "POST") {
    const auto fields = parseFlatJsonObject(requestBody);
    if (!fields.has_value()) {
      return response(400, "Bad Request", "application/json; charset=utf-8",
                       "{\"error\":\"Request body must be a JSON object\"}");
    }

    const auto channelIt = fields->find("channel");
    const auto videoIdIt = fields->find("videoId");
    const auto startIt = fields->find("startMinute");
    const auto endIt = fields->find("endMinute");
    if (channelIt == fields->end() || channelIt->second.empty() ||
        videoIdIt == fields->end() || videoIdIt->second.empty() ||
        startIt == fields->end() || endIt == fields->end()) {
      return response(
          400,
          "Bad Request",
          "application/json; charset=utf-8",
          "{\"error\":\"channel, videoId, startMinute and endMinute are required\"}");
    }

    ProgramSlot slot;
    slot.channel = channelIt->second;
    slot.videoId = videoIdIt->second;
    try {
      slot.startMinute = std::stoi(startIt->second);
      slot.endMinute = std::stoi(endIt->second);
    } catch (const std::exception&) {
      return response(400, "Bad Request", "application/json; charset=utf-8",
                       "{\"error\":\"startMinute and endMinute must be integers\"}");
    }

    const auto result = schedule.addSlot(slot);
    if (result == Schedule::AddResult::kInvalidRange) {
      return response(
          422,
          "Unprocessable Entity",
          "application/json; charset=utf-8",
          "{\"error\":\"Slot must fall within a single day and endMinute must be after startMinute\"}");
    }
    if (result == Schedule::AddResult::kOverlap) {
      return response(
          409,
          "Conflict",
          "application/json; charset=utf-8",
          "{\"error\":\"Slot overlaps an existing programme on this channel\"}");
    }

    if (!scheduleFile.empty() && !schedule.saveToFile(scheduleFile)) {
      std::cerr << "warning: failed to persist schedule to " << scheduleFile << "\n";
    }

    return response(201, "Created", "application/json; charset=utf-8", scheduleSlotJson(slot));
  }

  if (method != "GET") {
    const std::string allow = path == "/api/schedule" ? "GET, POST" : "GET";
    return response(
        405,
        "Method Not Allowed",
        "text/plain; charset=utf-8",
        "Method not allowed",
        "Allow: " + allow + "\r\n");
  }

  if (path == "/api/schedule") {
    const auto channelParam = query.find("channel");
    const auto slots = channelParam == query.end()
                            ? schedule.slots()
                            : schedule.slotsForChannel(urlDecode(channelParam->second));
    return response(200, "OK", "application/json; charset=utf-8", scheduleSlotsJson(slots));
  }

  if (path == "/api/schedule/now") {
    const auto channelParam = query.find("channel");
    if (channelParam == query.end() || channelParam->second.empty()) {
      return response(400, "Bad Request", "application/json; charset=utf-8",
                       "{\"error\":\"channel query parameter is required\"}");
    }

    int minuteOfDay = 0;
    const auto minuteParam = query.find("minute");
    if (minuteParam != query.end()) {
      try {
        minuteOfDay = std::stoi(minuteParam->second);
      } catch (const std::exception&) {
        return response(400, "Bad Request", "application/json; charset=utf-8",
                         "{\"error\":\"minute must be an integer\"}");
      }
      if (minuteOfDay < 0 || minuteOfDay >= 24 * 60) {
        return response(400, "Bad Request", "application/json; charset=utf-8",
                         "{\"error\":\"minute must be between 0 and 1439\"}");
      }
    } else {
      const auto now = std::time(nullptr);
      std::tm localTime{};
#ifdef _WIN32
      localtime_s(&localTime, &now);
#else
      localtime_r(&now, &localTime);
#endif
      minuteOfDay = localTime.tm_hour * 60 + localTime.tm_min;
    }

    const auto slot = schedule.nowPlaying(urlDecode(channelParam->second), minuteOfDay);
    if (!slot.has_value()) {
      return response(200, "OK", "application/json; charset=utf-8", "null");
    }

    return response(200, "OK", "application/json; charset=utf-8", scheduleSlotJson(*slot));
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

  // Virtual HDHomeRun / tuner emulation endpoints. These mirror the JSON API
  // exposed by real HDHomeRun devices so DVR software such as Plex or Emby
  // can discover TVTime as a network tuner and stream channels through it.
  if (target == "/discover.json") {
    return response(200, "OK", "application/json; charset=utf-8", discoverJson(baseUrl));
  }

  if (target == "/lineup.json") {
    return response(200, "OK", "application/json; charset=utf-8", lineupJson(library, baseUrl));
  }

  if (target == "/lineup_status.json") {
    return response(200, "OK", "application/json; charset=utf-8", lineupStatusJson());
  }

  if (target.rfind("/api/stream/", 0) == 0) {
    const std::string videoId = urlDecode(target.substr(12));
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

    // IPTV (and other remote) sources are streamed by redirecting the client
    // directly to the origin URL rather than proxying the bytes ourselves.
    if (isRemoteUri(video->uri)) {
      return response(
          302,
          "Found",
          "text/plain; charset=utf-8",
          "",
          "Location: " + video->uri + "\r\n");
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

std::string trimHeaderValue(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string extractHeader(const std::string& headerBlock, const std::string& name) {
  std::istringstream headerStream(headerBlock);
  std::string line;
  // Skip the request line (method/target/version).
  std::getline(headerStream, line);

  while (std::getline(headerStream, line)) {
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    std::string key = line.substr(0, colon);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });

    if (key == name) {
      return trimHeaderValue(line.substr(colon + 1));
    }
  }

  return "";
}

void handleClient(
    int clientDescriptor,
    const std::filesystem::path& documentRoot,
    std::shared_ptr<const MediaLibrary> library,
    std::shared_ptr<Schedule> schedule,
    const std::filesystem::path& scheduleFile) {
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

  const std::string headerBlock = requestText.substr(0, headerEnd);
  const std::string hostHeader = extractHeader(headerBlock, "host");
  const std::string contentLengthHeader = extractHeader(headerBlock, "content-length");

  std::string body = requestText.substr(headerEnd + 4);
  if (!contentLengthHeader.empty()) {
    std::size_t contentLength = 0;
    try {
      contentLength = static_cast<std::size_t>(std::stoul(contentLengthHeader));
    } catch (const std::exception&) {
      contentLength = 0;
    }

    contentLength = std::min<std::size_t>(contentLength, 1 << 20);
    while (body.size() < contentLength) {
      const auto bytesToRead =
          std::min(buffer.size(), contentLength - body.size());
      const auto bytesRead = recv(client.descriptor(), buffer.data(), bytesToRead, 0);
      if (bytesRead <= 0) {
        break;
      }
      body.append(buffer.data(), static_cast<std::size_t>(bytesRead));
    }
    body.resize(std::min(body.size(), contentLength));
  }

  const auto output =
      route(documentRoot, *library, *schedule, scheduleFile, method, target, hostHeader, body);
  sendResponse(client.descriptor(), output);
}

#endif

}  // namespace

HttpServer::HttpServer(
    std::filesystem::path documentRoot,
    std::shared_ptr<MediaLibrary> library,
    std::shared_ptr<Schedule> schedule,
    std::filesystem::path scheduleFile)
    : documentRoot_(std::filesystem::absolute(std::move(documentRoot))),
      library_(std::move(library)),
      schedule_(std::move(schedule)),
      scheduleFile_(std::move(scheduleFile)) {
  if (!scheduleFile_.empty() && !schedule_->loadFromFile(scheduleFile_)) {
    std::cerr << "warning: failed to load persisted schedule from " << scheduleFile_ << "\n";
  }
}

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
        [clientDescriptor,
         documentRoot = documentRoot_,
         library = library_,
         schedule = schedule_,
         scheduleFile = scheduleFile_,
         activeClients]() {
          handleClient(clientDescriptor, documentRoot, library, schedule, scheduleFile);
          activeClients->fetch_sub(1);
        })
        .detach();
  }
#endif
}

}  // namespace tvtime::server
