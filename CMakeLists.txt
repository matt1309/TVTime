cmake_minimum_required(VERSION 3.16)

project(TVTime VERSION 0.1.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tvtime_core
  src/core/media_library.cpp
  src/core/schedule.cpp
  src/plugins/local_file_source.cpp
)

target_include_directories(tvtime_core PUBLIC include)

add_executable(tvtime_server
  src/server/http_server.cpp
  src/server/main.cpp
)

target_link_libraries(tvtime_server PRIVATE tvtime_core Threads::Threads)

if(MSVC)
  target_compile_options(tvtime_core PRIVATE /W4)
  target_compile_options(tvtime_server PRIVATE /W4)
else()
  target_compile_options(tvtime_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tvtime_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
