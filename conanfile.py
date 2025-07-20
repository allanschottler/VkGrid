from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain

class Conan(ConanFile):
    name = "vkgrid"
    version = "1.0"
    license = "MIT"
    author = "Allan Schöttler"
    url = "https://github.com/allanschottler/VkGrid"
    description = "Vulkan Grid"
    topics = ("Vulkan", "C++")
    
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
    default_options = {
        "boost/*:shared": False
    }

    def requirements(self):
        self.requires("qt/6.8.3", options={
            "gui": True,
            "widgets": True,
            "with_vulkan": True,
            "with_zstd": True,
            "shared": False
        })
        self.requires("vulkan-loader/1.3.239.0")
        self.requires("vulkan-headers/1.3.239.0")
        self.requires("boost/1.85.0", options={
            "shared": False
        })
        self.requires("zstd/1.5.5")

    def layout(self):
        cmake_layout(self)
    
    
