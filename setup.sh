#!/bin/bash
set -e

# Install required Python packages for building
pip install conan cmake

# Configure default Conan profile
conan profile detect --force --name default

# Install dependencies with Conan
conan install . --build=missing -s compiler=gcc -s compiler.version=14 -s compiler.libcxx=libstdc++ -s compiler.cppstd=23 -c tools.system.package_manager:mode=install

# Configure the project with CMake using the Conan toolchain
cd build/Release/generators
cmake ../../.. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE="conan_toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
