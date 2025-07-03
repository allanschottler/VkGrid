cmake_minimum_required(VERSION 3.16)
project(vkgrid CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall)

find_package(Vulkan REQUIRED)
find_package(Boost REQUIRED COMPONENTS system json)
find_package(zstd CONFIG)

include_directories(include)

add_executable(vkgrid_server vkgrid_server.cpp)
target_link_libraries(vkgrid_server
    PRIVATE
      Vulkan::Vulkan
      Boost::system
      Boost::json
      zstd
)

add_executable(vkgrid_client vkgrid_client.cpp)
target_link_libraries(vkgrid_client
    PRIVATE
      Boost::system
      Boost::json
      zstd
)

if(WIN32)
  # On Windows, link against Winsock2 for any socket use
  target_link_libraries(vkgrid_server PRIVATE ws2_32)
  target_link_libraries(vkgrid_client PRIVATE ws2_32)
endif()
