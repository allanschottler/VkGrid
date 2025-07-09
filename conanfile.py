from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout

class Conan(ConanFile):
    name = "vkgrid"
    version = "1.0"
    license = "MIT"
    author = "Allan Schöttler"
    # url = "https://github.com/allanschottler/VkGrid"
    description = "Vulkan Grid"
    topics = ("Vulkan", "C++")
    
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
    exports_sources = [
        "CMakeLists.txt",
        "include/*",
    ]

    requires = [
        #"vkeng/1.0@me/dev",
        "vulkan-headers/1.4.309.0",
        "vulkan-loader/1.4.309.0",
        "zstd/1.5.7",
        "qt/6.5.3",
        "boost/1.85.0",
    ]
    
    def configure(self):
        pass

    def layout(self):
        cmake_layout(self)
    
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.includedirs = ["include"]
    
