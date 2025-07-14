#define VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#include <vulkan/vulkan.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <zstd.h>
#include <vector>
#include <array>
#include <chrono>
#include <thread>
#include <boost/log/trivial.hpp>
#include "logging.h"
#include <cmath>
#include <csignal>
#include <atomic>

using tcp = boost::asio::ip::tcp;
using udp = boost::asio::ip::udp;

std::atomic_bool serverRunning{true};

void handleSignal(int){
    serverRunning.store(false);
}

struct VulkanOffscreen {
    vk::UniqueInstance      instance;
    vk::UniqueDevice        device;
    vk::Queue               queue;
    uint32_t                qfi;

    // Offscreen targets
    vk::UniqueImage         colorImage;
    vk::UniqueDeviceMemory  colorImageMemory;
    vk::UniqueImageView     colorImageView;
    vk::UniqueRenderPass    renderPass;
    vk::UniqueFramebuffer   framebuffer;
    vk::UniqueCommandPool   cmdPool;

    // Staging for readback
    vk::UniqueBuffer        stagingBuf;
    vk::UniqueDeviceMemory  stagingMem;

    const uint32_t          width = 800, height = 600;

    VulkanOffscreen() {
        //validation layer
        std::vector<const char*> layers = {"VK_LAYER_KHRONOS_validation"};
        
        // Instance & device setup
        vk::ApplicationInfo appInfo{"OffscreenClear", VK_MAKE_VERSION(1,2,0), "NoEngine", 0, VK_API_VERSION_1_2};
        vk::InstanceCreateInfo ici{{}, &appInfo, static_cast<uint32_t>(layers.size()), layers.data(), 0, nullptr};
        instance = vk::createInstanceUnique(ici);
        // instance = vk::createInstanceUnique({{}, &appInfo, {}, {}});
        auto phys = instance->enumeratePhysicalDevices().front();
        auto families = phys.getQueueFamilyProperties();
        for (uint32_t i = 0; i < families.size(); ++i) {
            if (families[i].queueFlags & vk::QueueFlagBits::eGraphics) { qfi = i; break; }
        }
        float prio = 1.0f;
        vk::DeviceQueueCreateInfo dqci{{}, qfi, 1, &prio};
        device = phys.createDeviceUnique({{}, 1, &dqci});
        queue  = device->getQueue(qfi, 0);
        cmdPool = device->createCommandPoolUnique({{}, qfi});

        // Create offscreen color image
        vk::Format fmt = vk::Format::eR8G8B8A8Unorm;
        vk::ImageCreateInfo imgInfo{{}, vk::ImageType::e2D, fmt,
            {width, height, 1}, 1, 1, vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc};
        colorImage = device->createImageUnique(imgInfo);
        auto memReq = device->getImageMemoryRequirements(*colorImage);
        auto memProps = phys.getMemoryProperties();
        uint32_t idx = 0;
        for (; idx < memProps.memoryTypeCount; ++idx) {
            if ((memReq.memoryTypeBits & (1u << idx)) && (memProps.memoryTypes[idx].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal))
                break;
        }
        colorImageMemory = device->allocateMemoryUnique({memReq.size, idx});
        device->bindImageMemory(*colorImage, *colorImageMemory, 0);
        vk::ImageViewCreateInfo ivInfo{{}, *colorImage, vk::ImageViewType::e2D, fmt,
            {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        colorImageView = device->createImageViewUnique(ivInfo);

        // Render pass: clear only
        vk::AttachmentDescription attachDesc{ {}, fmt, vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal  // changed here
        };
        vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
        vk::SubpassDescription subpass{{}, vk::PipelineBindPoint::eGraphics,
            0, nullptr, 1, &colorRef};
        vk::RenderPassCreateInfo rpInfo{{}, 1, &attachDesc, 1, &subpass};
        renderPass = device->createRenderPassUnique(rpInfo);

        // Framebuffer
        vk::FramebufferCreateInfo fbInfo{{}, *renderPass, 1, &*colorImageView, width, height, 1};
        framebuffer = device->createFramebufferUnique(fbInfo);

        // Staging buffer for readback
        vk::BufferCreateInfo bufInfo{{}, width * height * 4, vk::BufferUsageFlagBits::eTransferDst};
        stagingBuf = device->createBufferUnique(bufInfo);
        auto bufReq = device->getBufferMemoryRequirements(*stagingBuf);
        idx = 0;
        for (; idx < memProps.memoryTypeCount; ++idx) {
            auto flags = memProps.memoryTypes[idx].propertyFlags;
            if ((bufReq.memoryTypeBits & (1u << idx)) && (flags & vk::MemoryPropertyFlagBits::eHostVisible) && (flags & vk::MemoryPropertyFlagBits::eHostCoherent))
                break;
        }
        stagingMem = device->allocateMemoryUnique({bufReq.size, idx});
        device->bindBufferMemory(*stagingBuf, *stagingMem, 0);
    }

    std::vector<uint8_t> renderFrame(float time) {
        auto cbs = device->allocateCommandBuffersUnique({*cmdPool, vk::CommandBufferLevel::ePrimary, 1});
        vk::CommandBuffer cb = *cbs.front();
        cb.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

        // Transition to color attachment
        vk::ImageMemoryBarrier barrier1{{}, vk::AccessFlagBits::eColorAttachmentWrite,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, *colorImage,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            {}, nullptr, nullptr, barrier1);

        // Begin & end render pass (clear only)
        vk::ClearValue clearColor{{(std::sin(time) + 1.0f) * 0.5f, 0.2f, 0.3f, 1.0f}};
        vk::RenderPassBeginInfo rpbi{*renderPass, *framebuffer,
            {{0,0}, {width, height}}, 1, &clearColor};
        cb.beginRenderPass(rpbi, vk::SubpassContents::eInline);
        cb.endRenderPass();

        // Transition to transfer src
        vk::ImageMemoryBarrier barrier2{
            vk::AccessFlagBits::eColorAttachmentWrite,
            vk::AccessFlagBits::eTransferRead,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eTransferSrcOptimal,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            *colorImage,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlagBits::eByRegion,
            nullptr, nullptr, barrier2);

        // Copy to staging
        vk::BufferImageCopy region{0,0,0,
            {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            {0,0,0}, {width, height, 1}};
        cb.copyImageToBuffer(*colorImage, vk::ImageLayout::eTransferSrcOptimal,
            *stagingBuf, 1, &region);
        cb.end();

        // Submit & wait
        vk::SubmitInfo submit{0, nullptr, nullptr, 1, &cb};
        queue.submit(submit, {});
        queue.waitIdle();

        // Read back pixels
        void* ptr = device->mapMemory(*stagingMem, 0, VK_WHOLE_SIZE);
        vk::MappedMemoryRange mrange(*stagingMem, 0, VK_WHOLE_SIZE);
        device->invalidateMappedMemoryRanges(1, &mrange);
        std::vector<uint8_t> pixels(width * height * 4);
        std::memcpy(pixels.data(), ptr, pixels.size());
        device->unmapMemory(*stagingMem);

        return pixels;
    }

  private:
    static uint32_t findMemoryType(vk::PhysicalDevice phys, uint32_t typeFilter, vk::MemoryPropertyFlags flags) {
        auto memProps = phys.getMemoryProperties();
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        }
        throw std::runtime_error("No suitable memory type");
    }
};

void discoveryThread(unsigned short port) {
    boost::asio::io_context ctx;
    udp::socket sock(ctx);
    sock.open(udp::v4());
    sock.set_option(boost::asio::socket_base::reuse_address(true));
    sock.set_option(boost::asio::socket_base::broadcast(true));
    sock.bind(udp::endpoint(udp::v4(), 5001));

    udp::endpoint broadcast_ep(boost::asio::ip::address_v4::broadcast(), 5001);
    auto next_announce = std::chrono::steady_clock::now();

    sock.non_blocking(true);
    while (serverRunning.load()) {
        std::array<char, 64> buf{};
        udp::endpoint sender;
        boost::system::error_code ec;
        size_t len = sock.receive_from(boost::asio::buffer(buf), sender, 0, ec);
        if (!ec && len > 0) {
            std::string msg(buf.data(), len);
            if (msg == "DISCOVER_VKGRID_SERVER") {
                std::string resp = "VKGRID_SERVER:" + std::to_string(port);
                sock.send_to(boost::asio::buffer(resp), broadcast_ep);
                BOOST_LOG_TRIVIAL(info) << "Sent discovery response to " << sender.address().to_string();
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= next_announce) {
            std::string resp = "VKGRID_SERVER:" + std::to_string(port);
            sock.send_to(boost::asio::buffer(resp), broadcast_ep);
            BOOST_LOG_TRIVIAL(info) << "Broadcast ping";
            next_announce = now + std::chrono::seconds(2);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    sock.close();
}

void session(tcp::socket sock) {
    VulkanOffscreen gfx;
    float t = 0.0f;
    BOOST_LOG_TRIVIAL(info) << "New session started from " << sock.remote_endpoint();

    try {
        while (serverRunning.load() && sock.is_open()) {
           auto pixels = gfx.renderFrame(t);
            t += 0.016f;
            uint32_t orig = pixels.size();
            size_t maxC = ZSTD_compressBound(orig);
            std::vector<char> cbuf(maxC);
            size_t csize = ZSTD_compress(cbuf.data(), maxC, pixels.data(), orig, 1);
            if (ZSTD_isError(csize)) continue;
            uint32_t no = htonl(orig), nc = htonl(static_cast<uint32_t>(csize));
            boost::asio::write(sock, boost::asio::buffer(&no, sizeof(no)));
            boost::asio::write(sock, boost::asio::buffer(&nc, sizeof(nc)));
            boost::asio::write(sock, boost::asio::buffer(cbuf.data(), csize));
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    } catch (const boost::system::system_error& e) {
        if(e.code() != boost::asio::error::operation_aborted &&
           e.code() != boost::asio::error::interrupted &&
           e.code() != boost::asio::error::eof &&
           e.code() != boost::asio::error::connection_reset &&
           e.code() != boost::asio::error::connection_aborted)
            BOOST_LOG_TRIVIAL(error) << "Session error: " << e.what();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Session error: " << e.what();
    }

    BOOST_LOG_TRIVIAL(info) << "Session ended for " << sock.remote_endpoint();
    boost::system::error_code ec;
    sock.shutdown(tcp::socket::shutdown_both, ec);
    sock.close(ec);
}

int main(int argc, char* argv[]) {
    unsigned short port = 5000;
    if (argc > 1) {
        port = static_cast<unsigned short>(std::stoi(argv[1]));
    }

    signal(SIGINT, handleSignal);

    init_logging("Server");

    std::thread dThread(discoveryThread, port);
    boost::asio::io_context ctx;
    tcp::acceptor acceptor(ctx, {tcp::v4(), port});
    acceptor.non_blocking(true);
    BOOST_LOG_TRIVIAL(info) << "Listening on port " << port;
    BOOST_LOG_TRIVIAL(info) << "Discovery on UDP port 5001";
    while (serverRunning.load()) {
        tcp::socket sock(ctx);
        boost::system::error_code ec;
        acceptor.accept(sock, ec);
        if(ec) {
            if(ec == boost::asio::error::would_block) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            else {
                BOOST_LOG_TRIVIAL(error) << "Accept error: " << ec.message();
                break;
            }
        }
        std::thread(session, std::move(sock)).detach();
    }
    acceptor.close();
    serverRunning.store(false);
    dThread.join();
    BOOST_LOG_TRIVIAL(info) << "Shutting down";
    return 0;
}
