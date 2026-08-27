#include "tvtime/plugins/dlna_source.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

namespace tvtime {

namespace {

constexpr const char* kSsdpMulticastAddr = "239.255.255.250";
constexpr int kSsdpPort = 1900;
constexpr int kSsdpTimeoutMs = 3000;

std::string createSsdpSearch() {
  std::ostringstream msg;
  msg << "M-SEARCH * HTTP/1.1\r\n"
      << "HOST: " << kSsdpMulticastAddr << ":" << kSsdpPort << "\r\n"
      << "MAN: \"ssdp:discover\"\r\n"
      << "MX: 3\r\n"
      << "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"
      << "\r\n";
  return msg.str();
}

std::string extractLocation(const std::string& response) {
  std::istringstream stream(response);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.rfind("LOCATION:", 0) == 0 || line.rfind("Location:", 0) == 0) {
      const auto colonPos = line.find(':');
      if (colonPos != std::string::npos) {
        std::string location = line.substr(colonPos + 1);
        location.erase(0, location.find_first_not_of(" \t\r\n"));
        location.erase(location.find_last_not_of(" \t\r\n") + 1);
        return location;
      }
    }
  }
  return "";
}

}  // namespace

DlnaSource::DlnaSource() = default;

std::string DlnaSource::name() const {
  return "dlna";
}

std::vector<Video> DlnaSource::discover() {
  std::vector<Video> videos;
  
  const auto devices = discoverDevices();
  for (const auto& deviceUrl : devices) {
    const auto deviceVideos = browseDevice(deviceUrl);
    videos.insert(videos.end(), deviceVideos.begin(), deviceVideos.end());
  }
  
  return videos;
}

std::vector<std::string> DlnaSource::discoverDevices() {
  std::vector<std::string> devices;

#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "DLNA: WSAStartup failed\n";
    return devices;
  }
#endif

  const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    std::cerr << "DLNA: Failed to create socket\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return devices;
  }

  sockaddr_in multicastAddr{};
  multicastAddr.sin_family = AF_INET;
  multicastAddr.sin_port = htons(kSsdpPort);
  multicastAddr.sin_addr.s_addr = inet_addr(kSsdpMulticastAddr);

  const std::string searchMsg = createSsdpSearch();
  const auto sent = sendto(
      sock,
      searchMsg.c_str(),
      static_cast<int>(searchMsg.size()),
      0,
      reinterpret_cast<sockaddr*>(&multicastAddr),
      sizeof(multicastAddr));

  if (sent < 0) {
    std::cerr << "DLNA: Failed to send SSDP search\n";
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return devices;
  }

  const auto startTime = std::chrono::steady_clock::now();
  char buffer[4096];

  while (true) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);
    if (elapsed.count() >= kSsdpTimeoutMs) {
      break;
    }

#ifdef _WIN32
    const DWORD timeout = kSsdpTimeoutMs - static_cast<DWORD>(elapsed.count());
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = (kSsdpTimeoutMs - elapsed.count()) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    sockaddr_in responseAddr{};
#ifdef _WIN32
    int addrLen = sizeof(responseAddr);
#else
    socklen_t addrLen = sizeof(responseAddr);
#endif

    const auto received = recvfrom(
        sock,
        buffer,
        sizeof(buffer) - 1,
        0,
        reinterpret_cast<sockaddr*>(&responseAddr),
        &addrLen);

    if (received > 0) {
      buffer[received] = '\0';
      const std::string response(buffer);
      const auto location = extractLocation(response);
      if (!location.empty()) {
        if (std::find(devices.begin(), devices.end(), location) == devices.end()) {
          devices.push_back(location);
          std::cout << "DLNA: Discovered device at " << location << "\n";
        }
      }
    }
  }

#ifdef _WIN32
  closesocket(sock);
  WSACleanup();
#else
  close(sock);
#endif

  return devices;
}

std::vector<Video> DlnaSource::browseDevice(const std::string& deviceUrl) {
  std::vector<Video> videos;
  
  // This is a simplified placeholder implementation.
  // A full DLNA implementation would require:
  // 1. Fetching the device description XML from deviceUrl
  // 2. Parsing the XML to find the ContentDirectory service
  // 3. Making SOAP requests to Browse the content hierarchy
  // 4. Parsing the DIDL-Lite XML responses to extract media items
  
  // For now, we'll just log that we found a device
  std::cout << "DLNA: Would browse device at " << deviceUrl << "\n";
  std::cout << "DLNA: Full DLNA browsing requires XML and SOAP parsing\n";
  
  return videos;
}

}  // namespace tvtime
