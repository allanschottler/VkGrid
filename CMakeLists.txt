cmake_minimum_required(VERSION 3.16) 
project(vkgrid LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_VERBOSE_MAKEFILE ON)
# add_compile_options(-Wall)

if(WIN32)
  add_compile_definitions(_WIN32_WINNT=0x0A00)  # Windows 10
  # add_compile_definitions(NOMINMAX)             # Disable min/max macros
  # add_compile_definitions(UNICODE)               # Use Unicode
endif()

# add_compile_definitions(BOOST_ALL_STATIC_LINK)  # static linking for Boost.Log
# add_compile_definitions(BOOST_LOG_STATIC_LINK)  # use static libs for Boost.Log

set(CMAKE_AUTOMOC  ON)
set(CMAKE_AUTORCC  ON)
set(CMAKE_AUTOUIC  ON)

find_package(Qt6 6.8 CONFIG REQUIRED COMPONENTS Core Widgets Gui)
find_package(Vulkan REQUIRED)
find_package(Boost 1.85 CONFIG REQUIRED COMPONENTS 
  log log_setup filesystem thread json
  date_time regex chrono system atomic)

add_executable(vkgrid_server vkgrid_server.cpp)
target_link_libraries(vkgrid_server PRIVATE
  Vulkan::Vulkan
  Qt6::Core
  # Boost::disable_autolinking
  Boost::log_setup
  Boost::log
  Boost::system
  Boost::json
  Boost::thread
  Boost::date_time
  Boost::regex
  Boost::atomic
  Boost::filesystem
  Boost::chrono
  Shlwapi
  Advapi32
  Ws2_32
  Mswsock
)

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
  # Boost::disable_autolinking
  Boost::log_setup
  Boost::log
  Boost::system
  Boost::json
  Boost::thread
  Boost::date_time
  Boost::regex
  Boost::atomic
  Boost::filesystem
  Boost::chrono
  Shlwapi
  Advapi32
  Ws2_32
)
