#include <vulkan/vulkan.hpp>
#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <zstd.h>
#include <vector>
#include <cstring>
#include <iostream>

using udp = boost::asio::ip::udp;
namespace json = boost::json;

// Gather local GPU info via Vulkan
json::array query_gpus() {
    json::array arr;
    vk::ApplicationInfo appInfo{"UDP-GPU-Server", 1, "", 0, VK_API_VERSION_1_2};
    vk::InstanceCreateInfo instInfo({}, &appInfo);
    vk::UniqueInstance inst = vk::createInstanceUnique(instInfo);

    for (auto& dev : inst->enumeratePhysicalDevices()) {
        auto p = dev.getProperties();
        json::object obj;
        obj["deviceName"]      = p.deviceName;
        obj["vendorID"]        = p.vendorID;
        obj["deviceID"]        = p.deviceID;
        obj["apiVersionMajor"] = VK_VERSION_MAJOR(p.apiVersion);
        obj["apiVersionMinor"] = VK_VERSION_MINOR(p.apiVersion);
        arr.push_back(std::move(obj));
    }
    return arr;
}

int main() {
    boost::asio::io_context ctx;
    udp::socket sock(ctx);
    sock.open(udp::v4());
    sock.set_option(boost::asio::socket_base::reuse_address(true));
    sock.bind(udp::endpoint(udp::v4(), 4000));

    std::array<char,1> buf;
    udp::endpoint remote;
    for (;;) {
        // Receive ping
        sock.receive_from(boost::asio::buffer(buf), remote);

        // Serialize JSON
        auto gpus = query_gpus();
        std::string payload = json::serialize(gpus);
        uint32_t origSize = static_cast<uint32_t>(payload.size());

        // Compress with Zstd
        size_t maxCSize = ZSTD_compressBound(origSize);
        std::vector<char> cBuffer(maxCSize);
        size_t cSize = ZSTD_compress(cBuffer.data(), maxCSize,
                                     payload.data(), origSize,
                                     /* level */ 1);
        if (ZSTD_isError(cSize)) {
            std::cerr << "Zstd compress error: "
                      << ZSTD_getErrorName(cSize) << std::endl;
            continue;
        }

        // Prefix with original size (network order)
        uint32_t netSize = htonl(origSize);
        std::vector<char> sendBuf(sizeof(netSize) + cSize);
        std::memcpy(sendBuf.data(), &netSize, sizeof(netSize));
        std::memcpy(sendBuf.data() + sizeof(netSize), cBuffer.data(), cSize);

        // Send compressed payload
        sock.send_to(boost::asio::buffer(sendBuf), remote);
        std::cout << "Replied to " << remote.address().to_string()
                  << " (" << origSize << "->" << cSize << " bytes)" << std::endl;
    }
    return 0;
}