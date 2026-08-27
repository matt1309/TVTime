cmake_minimum_required(VERSION 3.16)

project(TVTime VERSION 0.1.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(tvtime_core
  src/core/media_library.cpp
  src/core/schedule.cpp
  src/plugins/local_file_source.cpp
  src/plugins/dlna_source.cpp
  src/plugins/iptv_source.cpp
)

target_include_directories(tvtime_core PUBLIC include)

add_executable(tvtime_server
  src/server/http_server.cpp
  src/server/main.cpp
)

target_link_libraries(tvtime_server PRIVATE tvtime_core Threads::Threads)
target_compile_features(tvtime_core PUBLIC cxx_std_17)
target_compile_features(tvtime_server PRIVATE cxx_std_17)

if(WIN32)
  target_link_libraries(tvtime_core PUBLIC ws2_32)
endif()

if(MSVC)
  target_compile_options(tvtime_core PRIVATE /W4)
  target_compile_options(tvtime_server PRIVATE /W4)
else()
  target_compile_options(tvtime_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tvtime_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
