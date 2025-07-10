cmake_minimum_required(VERSION 3.16)
project(vkgrid CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall)

# On Windows, pick a modern WinNT version before any Qt/Windows includes:
# if (WIN32)
#   add_compile_definitions(_WIN32_WINNT=0x0A00)   # target Win10 or adjust as needed
# endif()

find_package(Vulkan REQUIRED)
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Widgets Gui)
find_package(zstd CONFIG)
find_package(Boost REQUIRED COMPONENTS system json log log_setup)

# Enable Qt’s automoc/uic/etc so Qt headers + moc work:
set(CMAKE_AUTOMOC  ON)
set(CMAKE_AUTORCC  ON)
set(CMAKE_AUTOUIC  ON)

add_executable(vkgrid_server vkgrid_server.cpp)
target_link_libraries(vkgrid_server
    PRIVATE
      Vulkan::Vulkan
      Qt6::Core
      Boost::system
      Boost::json
      Boost::log
      Boost::log_setup
)

add_executable(vkgrid_client
    vkgrid_client.cpp
    discovery_window.h
    vulkan_window.h
)
target_link_libraries(vkgrid_client
    PRIVATE
      Vulkan::Vulkan
      Qt6::Widgets
      Qt6::Gui
      Qt6::Core
      Boost::system
      Boost::json
      Boost::log
      Boost::log_setup
)

if(WIN32)
  # On Windows, link against Winsock2 for any socket use
  target_link_libraries(vkgrid_server PRIVATE ws2_32 mswsock)
  target_link_libraries(vkgrid_client PRIVATE ws2_32)
endif()
