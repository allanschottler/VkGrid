#include <vulkan/vulkan.hpp>
#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <iostream>

using udp = boost::asio::ip::udp;
namespace json = boost::json;

// query local Vulkan GPUs and return a JSON array
json::array query_gpus() {
    json::array arr;

    vk::ApplicationInfo appInfo{"UDP-GPU-Server", 1, "", 0, VK_API_VERSION_1_2};
    vk::InstanceCreateInfo instInfo({}, &appInfo);
    vk::UniqueInstance inst = vk::createInstanceUnique(instInfo);

    for (auto& dev : inst->enumeratePhysicalDevices()) {
        auto p = dev.getProperties();
        json::object obj;
        obj["deviceName"]       = p.deviceName;
        obj["vendorID"]         = p.vendorID;
        obj["deviceID"]         = p.deviceID;
        obj["apiVersionMajor"]  = VK_VERSION_MAJOR(p.apiVersion);
        obj["apiVersionMinor"]  = VK_VERSION_MINOR(p.apiVersion);
        arr.push_back(std::move(obj));
    }
    return arr;
}

int main() {
    try {
        boost::asio::io_context ctx;
        udp::socket sock(ctx, udp::endpoint(udp::v4(), 4000));

        std::array<char,1> buf;
        udp::endpoint remote;

        for (;;) {
            sock.receive_from(boost::asio::buffer(buf), remote);

            json::array gpus = query_gpus();
            std::string payload = json::serialize(gpus);

            sock.send_to(boost::asio::buffer(payload), remote);
            std::cout << "Replied to "
                      << remote.address().to_string()
                      << " - " << gpus.size() << " device(s)\n";
        }
    } catch (std::exception& e) {
        std::cerr << "Server error: " << e.what() << "\n";
    }
}
