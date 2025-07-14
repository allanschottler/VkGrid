cmake_minimum_required(VERSION 3.16)
project(vkgrid LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall)

if (WIN32)
  add_compile_definitions(BOOST_LOG_DYN_LINK)
endif()

# Enable Qt’s automoc/uic
set(CMAKE_AUTOMOC  ON)
set(CMAKE_AUTORCC  ON)
set(CMAKE_AUTOUIC  ON)

# Conan’s toolchain will inject CMAKE_PREFIX_PATH etc.
# Remember to pass -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake
find_package(Qt6 6.8 CONFIG REQUIRED COMPONENTS Core Widgets Gui)
find_package(Vulkan       REQUIRED)
find_package(Boost 1.85 REQUIRED
  COMPONENTS
    system
    json
    thread
    date_time
    atomic
    log
    log_setup
)

# === Server ===
add_executable(vkgrid_server vkgrid_server.cpp)
target_link_libraries(vkgrid_server PRIVATE
  Vulkan::Vulkan
  Qt6::Core

  # Boost
  Boost::system
  Boost::json
  Boost::thread
  Boost::date_time
  Boost::atomic

  # Boost.Log
  Boost::log_setup
  Boost::log

  # Win32 shims for async sink & networking
  Shlwapi
  Advapi32
  Ws2_32
  Mswsock
)

# === Client ===
add_executable(vkgrid_client
  vkgrid_client.cpp
  discovery_window.h
  vulkan_window.h
)
target_link_libraries(vkgrid_client PRIVATE
  Vulkan::Vulkan
  Qt6::Widgets
  Qt6::Gui
  Qt6::Core

  # Boost
  Boost::system
  Boost::json
  Boost::thread
  Boost::date_time
  Boost::atomic

  # Boost.Log
  Boost::log_setup
  Boost::log

  # Win32 shims
  Shlwapi
  Advapi32
  Ws2_32
)
