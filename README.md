# VkGrid

Remote Vulkan rendering over the network. A headless **server** renders frames offscreen with Vulkan, compresses them with Zstandard and streams them over TCP. A Qt **client** discovers servers on the LAN and presents the stream in a Vulkan window.

> Early prototype: the server currently renders an animated clear color at 800×600, ~60 FPS.

## Requirements

- C++23 compiler, CMake ≥ 3.16, [Conan 2](https://conan.io)
- Vulkan-capable GPU/driver
- Dependencies (via Conan): Qt 6.8, Boost 1.85 (asio, log), Vulkan loader/headers 1.3, zstd 1.5

Currently targets **Windows** (links Winsock/Shlwapi).

## Build

```sh
conan install . --build=missing -s build_type=Release
cmake --preset conan-default
cmake --build --preset conan-release
```

## Run

```sh
vkgrid_server [port]   # TCP port, default 5000
vkgrid_client          # pick a server from the list and click Connect
```

Allow TCP `5000` and UDP `5001` through the firewall.

## License

MIT
