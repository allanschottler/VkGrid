#include <vulkan/vulkan.hpp>
#include <boost/asio.hpp>
#include <boost/json.hpp>
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
    // Bind UDP socket to port 4000 and allow address reuse
    udp::socket sock(ctx);
    sock.open(udp::v4());
    sock.set_option(boost::asio::socket_base::reuse_address(true));
    sock.bind(udp::endpoint(udp::v4(), 4000));

    std::array<char,1> buf;
    udp::endpoint remote;
    for (;;) {
        // Wait for a ping (any packet)
        sock.receive_from(boost::asio::buffer(buf), remote);

        // Query and serialize GPU info
        auto gpus = query_gpus();
        std::string payload = json::serialize(gpus);

        // Reply to the sender
        sock.send_to(boost::asio::buffer(payload), remote);
        std::cout << "Replied to " << remote.address().to_string()
                  << " with " << gpus.size() << " device(s)" << std::endl; 
    }
    return 0;
}