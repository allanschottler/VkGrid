#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

using udp = boost::asio::ip::udp;
namespace json = boost::json;

void query_server(const std::string& host, unsigned short port) {
    try {
        boost::asio::io_context ctx;
        udp::resolver resolver(ctx);
        auto endpoints = resolver.resolve(udp::v4(), host, std::to_string(port));

        udp::socket sock(ctx);
        sock.open(udp::v4());

        // send a 1‐byte ping
        char ping = 0;
        sock.send_to(boost::asio::buffer(&ping,1), *endpoints.begin());

        // await JSON reply
        std::vector<char> recv_buf(8192);
        udp::endpoint sender;
        size_t len = sock.receive_from(boost::asio::buffer(recv_buf), sender);
        std::string_view sv(recv_buf.data(), len);

        // parse and print
        json::value val = json::parse(sv);
        auto& arr = val.as_array();

        std::cout << "Server " << host << ":" << port
                  << " - " << arr.size() << " GPU(s):\n";

        for (auto& v : arr) {
            auto& o = v.as_object();
            std::cout << " >> " << o["deviceName"].as_string() 
                      << " (vendor " << o["vendorID"].to_number<uint32_t>()
                      << ", device " << o["deviceID"].to_number<uint32_t>()
                      << ", Vulkan " << o["apiVersionMajor"].to_number<int>()
                      << "." << o["apiVersionMinor"].to_number<int>() << ")\n";
        }
        std::cout << "\n";

    } catch (std::exception& e) {
        std::cerr << "Error querying " << host << ": " << e.what() << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: client <server1> [server2 ...]\n";
        return 1;
    }
    const unsigned short port = 4000;
    std::vector<std::thread> threads;

    for (int i = 1; i < argc; ++i) {
        threads.emplace_back(query_server, argv[i], port);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (auto& t : threads) t.join();
    return 0;
}
