#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <string_view>

using udp = boost::asio::ip::udp;
namespace json = boost::json;

int main() {
    boost::asio::io_context ctx;
    // Open a UDP socket for broadcast and reuse
    udp::socket sock(ctx);
    sock.open(udp::v4());
    sock.set_option(boost::asio::socket_base::broadcast(true));
    sock.set_option(boost::asio::socket_base::reuse_address(true));

    constexpr unsigned short port = 4000;
    // Use IPv4 broadcast address
    udp::endpoint broadcast_ep(
        boost::asio::ip::address_v4::broadcast(), port
    );

    // Send a one-byte ping
    char ping = 0;
    sock.send_to(boost::asio::buffer(&ping, 1), broadcast_ep);

    // Switch to non-blocking to collect multiple replies
    sock.non_blocking(true);

    std::vector<char> recv_buf(8192);
    udp::endpoint sender;
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(500);

    // Collect responses until timeout
    while (std::chrono::steady_clock::now() - start < timeout) {
        boost::system::error_code ec;
        size_t len = sock.receive_from(
            boost::asio::buffer(recv_buf), sender, 0, ec
        );
        if (ec == boost::asio::error::would_block) {
            // no data yet, yield
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (ec) {
            std::cerr << "Receive error: " << ec.message() << std::endl;
            break;
        }

        // Parse JSON reply
        std::string_view sv(recv_buf.data(), len);
        json::value val = json::parse(sv);
        auto& arr = val.as_array();

        std::cout << "Server " << sender.address().to_string()
                  << ":" << sender.port()
                  << " replied with " << arr.size() << " GPU(s):" << std::endl;

        for (auto& v : arr) {
            auto& o = v.as_object();
            std::cout << " >> " << o["deviceName"].as_string()
                      << " (vendor " << o["vendorID"].to_number<uint32_t>()
                      << ", device " << o["deviceID"].to_number<uint32_t>()
                      << ", Vulkan " << o["apiVersionMajor"].to_number<int>()
                      << "." << o["apiVersionMinor"].to_number<int>()
                      << ")" << std::endl;
        }
    }
    return 0;
}