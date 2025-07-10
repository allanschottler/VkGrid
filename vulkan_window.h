#pragma once

#include <QWindow>
#include <QVulkanInstance>
#include <QCloseEvent>
#include <QApplication>
#include <thread>
#include <mutex>
#include <deque>
#include <vector>
#include <atomic>
#include <memory>
#include <set>
#include <cstring>
#include <iostream>
#include <zstd.h>
#include <boost/asio.hpp>
#include <vulkan/vulkan.hpp>
#include "discovery_window.h"

class VulkanWindow : public QWindow {
    Q_OBJECT
public:
    explicit VulkanWindow(const ServerInfo &info, QWindow *parent = nullptr);
    ~VulkanWindow();

    void run(QApplication &app);

protected:
    void closeEvent(QCloseEvent *ev) override;

private:
    void networkThread(const std::string &host, const std::string &port);

    QVulkanInstance m_qInst;

    std::deque<std::vector<uint8_t>> m_queue;
    std::mutex m_mutex;
    std::atomic_bool m_running{true};
    std::thread m_netThread;
    std::shared_ptr<boost::asio::ip::tcp::socket> m_sock;

    ServerInfo m_info;
};

inline VulkanWindow::VulkanWindow(const ServerInfo &info, QWindow *parent)
    : QWindow(parent), m_info(info) {
    setSurfaceType(QSurface::VulkanSurface);
}

inline VulkanWindow::~VulkanWindow() {
    m_running = false;
    if (m_netThread.joinable())
        m_netThread.join();
    if (m_sock && m_sock->is_open()) {
        boost::system::error_code ec;
        m_sock->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        m_sock->close(ec);
    }
}

inline void VulkanWindow::closeEvent(QCloseEvent *ev) {
    m_running = false;
    QWindow::closeEvent(ev);
}

inline void VulkanWindow::networkThread(const std::string &host, const std::string &port) {
    using tcp = boost::asio::ip::tcp;
    try {
        while (m_running.load() && m_sock->is_open()) {
            uint32_t norig, nc;
            boost::asio::read(*m_sock, boost::asio::buffer(&norig, 4));
            boost::asio::read(*m_sock, boost::asio::buffer(&nc, 4));
            uint32_t orig = ntohl(norig), csize = ntohl(nc);
            std::vector<char> cbuf(csize);
            boost::asio::read(*m_sock, boost::asio::buffer(cbuf));
            std::vector<uint8_t> pixels(orig);
            size_t dsize = ZSTD_decompress(pixels.data(), orig, cbuf.data(), csize);
            if (ZSTD_isError(dsize)) {
                std::cerr << "[Client] decompress error\n";
                continue;
            }
            std::lock_guard<std::mutex> lk(m_mutex);
            m_queue.push_back(std::move(pixels));
        }
    } catch (const boost::system::system_error &e) {
        if (e.code() != boost::asio::error::operation_aborted &&
            e.code() != boost::asio::error::interrupted &&
            e.code() != boost::asio::error::eof &&
            e.code() != boost::asio::error::connection_reset &&
            e.code() != boost::asio::error::connection_aborted)
            std::cerr << "[Client] network error: " << e.what() << '\n';
    } catch (const std::exception &e) {
        std::cerr << "[Client] network error: " << e.what() << '\n';
    }
    m_running = false;
}

inline void VulkanWindow::run(QApplication &app) {
    using tcp = boost::asio::ip::tcp;
    const uint32_t WIDTH = 800, HEIGHT = 600;

    if (!m_qInst.create()) {
        std::cout << "Failed to create QVulkanInstance" << std::endl;
        return;
    }
    setVulkanInstance(&m_qInst);
    resize(WIDTH, HEIGHT);
    setTitle("Remote Client");
    show();

    vk::Instance instance(m_qInst.vkInstance());
    vk::SurfaceKHR surface(m_qInst.surfaceForWindow(this));

    auto physList = instance.enumeratePhysicalDevices();
    auto phys = physList.front();
    auto qProps = phys.getQueueFamilyProperties();
    int gfxFamI = -1, presFamI = -1;
    for (int i = 0; i < static_cast<int>(qProps.size()); ++i) {
        if (qProps[i].queueFlags & vk::QueueFlagBits::eGraphics) gfxFamI = i;
        if (phys.getSurfaceSupportKHR(i, surface)) presFamI = i;
    }
    uint32_t gfxFam = static_cast<uint32_t>(gfxFamI);
    uint32_t presFam = static_cast<uint32_t>(presFamI);
    std::set<uint32_t> fams{gfxFam, presFam};
    std::vector<uint32_t> famIdx(fams.begin(), fams.end());
    float qp = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> qcis;
    for (auto f : famIdx) qcis.emplace_back(vk::DeviceQueueCreateInfo({}, f, 1, &qp));
    const std::vector<const char *> devExts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    vk::PhysicalDeviceFeatures devFeatures{};
    vk::DeviceCreateInfo devCI(
        {}, static_cast<uint32_t>(qcis.size()), qcis.data(),
        0, nullptr,
        static_cast<uint32_t>(devExts.size()), devExts.data(),
        &devFeatures);
    vk::UniqueDevice device = phys.createDeviceUnique(devCI);
    vk::Queue graphicsQueue = device->getQueue(gfxFam, 0);
    vk::Queue presentQueue = device->getQueue(presFam, 0);
    vk::UniqueCommandPool commandPool = device->createCommandPoolUnique({{}, gfxFam});

    auto caps = phys.getSurfaceCapabilitiesKHR(surface);
    auto formats = phys.getSurfaceFormatsKHR(surface);
    vk::SurfaceFormatKHR surfFmt = formats.front();
    vk::Extent2D extent{WIDTH, HEIGHT};
    vk::UniqueSwapchainKHR swapchain = device->createSwapchainKHRUnique({
        {}, surface,
        caps.minImageCount,
        surfFmt.format, surfFmt.colorSpace,
        extent, 1,
        vk::ImageUsageFlagBits::eTransferDst,
        famIdx.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
        static_cast<uint32_t>(famIdx.size()), famIdx.data(),
        caps.currentTransform,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::PresentModeKHR::eFifo,
        VK_TRUE});
    auto swapImages = device->getSwapchainImagesKHR(*swapchain);

    vk::UniqueBuffer stagingBuffer = device->createBufferUnique({
        {}, WIDTH * HEIGHT * 4, vk::BufferUsageFlagBits::eTransferSrc});
    auto bufReq = device->getBufferMemoryRequirements(*stagingBuffer);
    auto memProps = phys.getMemoryProperties();
    uint32_t memIdx = 0;
    for (; memIdx < memProps.memoryTypeCount; ++memIdx) {
        auto f = memProps.memoryTypes[memIdx].propertyFlags;
        if ((bufReq.memoryTypeBits & (1u << memIdx)) &&
            (f & vk::MemoryPropertyFlagBits::eHostVisible) &&
            (f & vk::MemoryPropertyFlagBits::eHostCoherent))
            break;
    }
    vk::UniqueDeviceMemory stagingMem = device->allocateMemoryUnique({bufReq.size, memIdx});
    device->bindBufferMemory(*stagingBuffer, *stagingMem, 0);

    vk::SemaphoreCreateInfo sci{};
    auto imageAvail = device->createSemaphoreUnique(sci);
    auto renderDone = device->createSemaphoreUnique(sci);

    boost::asio::io_context ctx;
    m_sock = std::make_shared<tcp::socket>(ctx);
    boost::asio::connect(*m_sock, tcp::resolver(ctx).resolve(m_info.address, m_info.port));
    m_netThread = std::thread(&VulkanWindow::networkThread, this, m_info.address, m_info.port);

    while (isVisible() && m_running.load()) {
        app.processEvents();
        std::vector<uint8_t> pixels;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (!m_queue.empty()) {
                pixels = std::move(m_queue.front());
                m_queue.pop_front();
            }
        }
        if (pixels.empty()) continue;
        void *dst = device->mapMemory(*stagingMem, 0, pixels.size());
        std::memcpy(dst, pixels.data(), pixels.size());
        device->unmapMemory(*stagingMem);
        auto [_, imgIndex] = device->acquireNextImageKHR(
            *swapchain, UINT64_MAX,
            *imageAvail, nullptr);
        auto cmdBuffers = device->allocateCommandBuffersUnique({
            *commandPool, vk::CommandBufferLevel::ePrimary, 1});
        auto cmdBuf = cmdBuffers.front().get();
        cmdBuf.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        cmdBuf.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer,
            {}, nullptr, nullptr,
            vk::ImageMemoryBarrier{
                {}, vk::AccessFlagBits::eTransferWrite,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                swapImages[imgIndex], range});
        vk::BufferImageCopy bic{
            0, 0, 0,
            {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            {0, 0, 0},
            {WIDTH, HEIGHT, 1}};
        cmdBuf.copyBufferToImage(
            *stagingBuffer,
            swapImages[imgIndex],
            vk::ImageLayout::eTransferDstOptimal,
            1, &bic);
        cmdBuf.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, nullptr, nullptr,
            vk::ImageMemoryBarrier{
                vk::AccessFlagBits::eTransferWrite, {},
                vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::ePresentSrcKHR,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                swapImages[imgIndex], range});
        cmdBuf.end();
        vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eTransfer;
        vk::SubmitInfo si(
            1, &*imageAvail, &waitStage,
            1, &cmdBuf,
            1, &*renderDone);
        graphicsQueue.submit(si, nullptr);
        vk::PresentInfoKHR pi(
            1, &*renderDone,
            1, &*swapchain, &imgIndex);
        presentQueue.presentKHR(pi);
        device->waitIdle();
    }

    m_running = false;
}
