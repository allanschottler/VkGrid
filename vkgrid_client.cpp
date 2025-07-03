#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <string_view>
#include <zstd.h>

using udp = boost::asio::ip::udp;
namespace json = boost::json;

auto query_local_ep(boost::asio::io_context& ctx) {
    udp::resolver resolver(ctx);
    auto ep_iter = resolver.resolve("8.8.8.8", "53");
    udp::endpoint test_ep = *ep_iter.begin();
    udp::socket temp_sock(ctx, udp::v4());
    temp_sock.connect(test_ep);
    auto local_ep = temp_sock.local_endpoint();
    return local_ep;
}

int main() {
    boost::asio::io_context ctx;
    // Open a UDP socket for broadcast and reuse
    udp::socket sock(ctx);
    sock.open(udp::v4());
    sock.set_option(boost::asio::socket_base::broadcast(true));
    auto local_ep = query_local_ep(ctx);
    auto local_ip   = local_ep.address().to_v4();
    uint32_t ip_u   = local_ip.to_uint();
    uint32_t mask_u = 0xFFFFFF00;                // /24
    uint32_t b_u    = (ip_u & mask_u) | ~mask_u; // compute broadcast
    auto bcast_addr = boost::asio::ip::address_v4{b_u};

    constexpr unsigned short port = 4000;
    udp::endpoint broadcast_ep(bcast_addr, port);

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
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (ec) {
            std::cerr << "Receive error: " << ec.message() << std::endl;
            break;
        }

        if (len < sizeof(uint32_t)) continue;
        uint32_t netSize;
        std::memcpy(&netSize, recv_buf.data(), sizeof(netSize));
        uint32_t origSize = ntohl(netSize);

        // Decompress
        std::vector<char> dBuffer(origSize);
        size_t dSize = ZSTD_decompress(dBuffer.data(), origSize,
                                       recv_buf.data() + sizeof(netSize),
                                       len - sizeof(netSize));
        if (ZSTD_isError(dSize)) {
            std::cerr << "Zstd decompress error: "
                      << ZSTD_getErrorName(dSize) << std::endl;
            continue;
        }

        // Parse JSON
        std::string_view sv(dBuffer.data(), origSize);
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