#include <vulkan/vulkan.hpp>
#include <QApplication>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDialog>
#include <QWindow>
#include <QVulkanInstance>
#include <QTimer>
#include <QSet>
#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <zstd.h>

#include <thread>
#include <mutex>
#include <chrono>
#include <deque>
#include <vector>
#include <set>
#include <string_view>
#include <iostream>
#include <atomic>
#include <csignal>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <arpa/inet.h>
#endif

struct ServerInfo {
    std::string address;
    std::string port;
};

std::deque<std::vector<uint8_t>> frameQueue;
std::mutex queueMutex;

struct DiscoveredServer {
    ServerInfo info;
    std::chrono::steady_clock::time_point last_seen;
};

std::vector<DiscoveredServer> discoveredServers;
std::mutex discoveryMutex;
std::atomic_bool discoveryRun{true};
std::shared_ptr<boost::asio::ip::tcp::socket> globalSock;
std::atomic_bool clientRunning{true};

void handleSignal(int){
    discoveryRun = false;
    clientRunning.store(false);
    if(globalSock && globalSock->is_open())
        globalSock->shutdown(boost::asio::ip::tcp::socket::shutdown_both);
}

void discoveryThreadFunc() {
    using udp = boost::asio::ip::udp;
    boost::asio::io_context ctx;
    udp::socket sock(ctx);
    sock.open(udp::v4());
    sock.set_option(boost::asio::socket_base::reuse_address(true));
    sock.set_option(boost::asio::socket_base::broadcast(true));
    // listen on the well-known discovery port so we can receive broadcast
    // announcements from any servers
    sock.bind(udp::endpoint(udp::v4(), 5001));

    udp::endpoint broadcast_ep(boost::asio::ip::address_v4::broadcast(), 5001);
    auto next_ping = std::chrono::steady_clock::now();
    sock.non_blocking(true);

    while (discoveryRun.load()) {
        std::array<char, 64> buf{};
        udp::endpoint sender;
        boost::system::error_code ec;
        size_t len = sock.receive_from(boost::asio::buffer(buf), sender, 0, ec);
        if (!ec && len > 0) {
            std::string msg(buf.data(), len);
            constexpr std::string_view prefix = "VKGRID_SERVER:";
            if (msg.rfind(prefix, 0) == 0) {
                std::string port = msg.substr(prefix.size());
                auto now = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lk(discoveryMutex);
                bool found = false;
                for (auto& s : discoveredServers) {
                    if (s.info.address == sender.address().to_string() && s.info.port == port) {
                        s.last_seen = now;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    discoveredServers.push_back({{sender.address().to_string(), port}, now});
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= next_ping) {
            std::string msg = "DISCOVER_VKGRID_SERVER";
            sock.send_to(boost::asio::buffer(msg), broadcast_ep);
            next_ping = now + std::chrono::seconds(1);
        }

        {
            std::lock_guard<std::mutex> lk(discoveryMutex);
            auto it = discoveredServers.begin();
            while (it != discoveredServers.end()) {
                if (now - it->last_seen > std::chrono::seconds(5))
                    it = discoveredServers.erase(it);
                else
                    ++it;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void networkThread(std::shared_ptr<boost::asio::ip::tcp::socket> sock,
                   const std::string& host, const std::string& port) {
    using tcp = boost::asio::ip::tcp;
    std::cout << "[Client] Connected to " << host << ":" << port << "\n";

    try {
        while (clientRunning.load() && sock->is_open()) {
        uint32_t norig, nc;
        boost::asio::read(*sock, boost::asio::buffer(&norig, 4));
        boost::asio::read(*sock, boost::asio::buffer(&nc,    4));
        uint32_t orig = ntohl(norig), csize = ntohl(nc);

        std::vector<char> cbuf(csize);
        boost::asio::read(*sock, boost::asio::buffer(cbuf));
        std::vector<uint8_t> pixels(orig);
        size_t dsize = ZSTD_decompress(pixels.data(), orig,
                                       cbuf.data(), csize);
        if (ZSTD_isError(dsize)) {
            std::cerr << "[Client] decompress error\n";
            continue;
        }

        std::lock_guard<std::mutex> lk(queueMutex);
        frameQueue.push_back(std::move(pixels));
        }
    } catch(const boost::system::system_error& e) {
        if(e.code() != boost::asio::error::operation_aborted &&
           e.code() != boost::asio::error::eof &&
           e.code() != boost::asio::error::connection_reset)
            std::cerr << "[Client] network error: " << e.what() << '\n';
    } catch(const std::exception& e) {
        std::cerr << "[Client] network error: " << e.what() << '\n';
    }

    boost::system::error_code ec;
    if(sock->is_open()) {
        sock->shutdown(tcp::socket::shutdown_both, ec);
        sock->close(ec);
    }
    std::cout << "[Client] Disconnected from " << host << ":" << port << "\n";
}

int main(int argc, char **argv){
    std::cout << "VkGrid Remote Client\n";
    const uint32_t WIDTH=800, HEIGHT=600;
    QApplication app(argc, argv);

    std::thread discoverThread(discoveryThreadFunc);

    // Qt dialog for server selection
    QDialog dlg;
    QVBoxLayout vlayout(&dlg);
    QListWidget list;
    vlayout.addWidget(&list);
    QPushButton btn("Connect");
    vlayout.addWidget(&btn);
    QObject::connect(&btn, &QPushButton::clicked, &dlg, &QDialog::accept);

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&](){
        std::lock_guard<std::mutex> lk(discoveryMutex);
        QSet<QString> current;
        for(const auto& s : discoveredServers){
            QString text = QString::fromStdString(s.info.address+":"+s.info.port);
            current.insert(text);
            if(list.findItems(text, Qt::MatchExactly).isEmpty())
                list.addItem(text);
        }

        for(int i=0;i<list.count();){
            if(!current.contains(list.item(i)->text())){
                delete list.takeItem(i);
            } else {
                ++i;
            }
        }
    });
    timer.start(500);

    if(dlg.exec()!=QDialog::Accepted || list.currentRow()<0){
        discoveryRun = false;
        discoverThread.join();
        return 0;
    }

    discoveryRun = false;
    discoverThread.join();
    size_t idx = static_cast<size_t>(list.currentRow());
    ServerInfo chosen;
    {
        std::lock_guard<std::mutex> lk(discoveryMutex);
        if(idx >= discoveredServers.size()) return 0;
        chosen = discoveredServers[idx].info;
    }

    boost::asio::io_context ctx;
    globalSock = std::make_shared<boost::asio::ip::tcp::socket>(ctx);
    boost::asio::connect(*globalSock,
        boost::asio::ip::tcp::resolver(ctx).resolve(chosen.address, chosen.port));

    signal(SIGINT, handleSignal);
    std::thread netThread(networkThread, globalSock, chosen.address, chosen.port);

    // Qt Vulkan window
    QVulkanInstance qInst;
    if(!qInst.create()) {
        std::cout << "Failed to create QVulkanInstance" << std::endl;
        return 0;
    }

    QWindow window;
    window.setSurfaceType(QSurface::VulkanSurface);
    window.setVulkanInstance(&qInst);
    window.resize(WIDTH, HEIGHT);
    window.setTitle("Remote Client");
    window.show();

    vk::Instance instance(qInst.vkInstance());
    vk::SurfaceKHR surface(qInst.surfaceForWindow(&window));

    auto physList = instance.enumeratePhysicalDevices();
    auto phys     = physList.front();
    auto qProps   = phys.getQueueFamilyProperties();

    int gfxFamI=-1, presFamI=-1;
    for (int i=0;i<(int)qProps.size();++i) {
        if (qProps[i].queueFlags & vk::QueueFlagBits::eGraphics) gfxFamI=i;
        if (phys.getSurfaceSupportKHR(i,surface)) presFamI=i;
    }
    uint32_t gfxFam = uint32_t(gfxFamI),
             presFam= uint32_t(presFamI);

    std::set<uint32_t> fams{gfxFam,presFam};
    std::vector<uint32_t> famIdx(fams.begin(),fams.end());
    float qp=1.0f;
    std::vector<vk::DeviceQueueCreateInfo> qcis;
    for(auto f:famIdx) qcis.emplace_back(vk::DeviceQueueCreateInfo({},f,1,&qp));

    const std::vector<const char*> devExts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    vk::PhysicalDeviceFeatures devFeatures{};
    vk::DeviceCreateInfo devCI(
        {}, (uint32_t)qcis.size(), qcis.data(),
        0,nullptr,
        (uint32_t)devExts.size(), devExts.data(),
        &devFeatures
    );
    vk::UniqueDevice device = phys.createDeviceUnique(devCI);
    vk::Queue graphicsQueue = device->getQueue(gfxFam,0);
    vk::Queue presentQueue  = device->getQueue(presFam,0);

    vk::UniqueCommandPool commandPool =
        device->createCommandPoolUnique({{},gfxFam});

    // Swapchain
    auto caps    = phys.getSurfaceCapabilitiesKHR(surface);
    auto formats= phys.getSurfaceFormatsKHR(surface);
    vk::SurfaceFormatKHR surfFmt = formats.front();
    vk::Extent2D extent{WIDTH,HEIGHT};
    vk::UniqueSwapchainKHR swapchain = device->createSwapchainKHRUnique({
        {}, surface,
        caps.minImageCount,
        surfFmt.format, surfFmt.colorSpace,
        extent, 1,
        vk::ImageUsageFlagBits::eTransferDst,
        famIdx.size()==1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
        (uint32_t)famIdx.size(), famIdx.data(),
        caps.currentTransform,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::PresentModeKHR::eFifo,
        VK_TRUE
    });
    auto swapImages = device->getSwapchainImagesKHR(*swapchain);

    // Staging for upload
    vk::UniqueBuffer stagingBuffer = device->createBufferUnique({
        {}, WIDTH*HEIGHT*4, vk::BufferUsageFlagBits::eTransferSrc
    });
    auto bufReq   = device->getBufferMemoryRequirements(*stagingBuffer);
    auto memProps = phys.getMemoryProperties();
    uint32_t memIdx=0;
    for (;memIdx<memProps.memoryTypeCount;++memIdx){
        auto f=memProps.memoryTypes[memIdx].propertyFlags;
        if ((bufReq.memoryTypeBits&(1u<<memIdx))&&
            (f&vk::MemoryPropertyFlagBits::eHostVisible)&&
            (f&vk::MemoryPropertyFlagBits::eHostCoherent))
            break;
    }
    vk::UniqueDeviceMemory stagingMem = device->allocateMemoryUnique({bufReq.size,memIdx});
    device->bindBufferMemory(*stagingBuffer,*stagingMem,0);

    // Semaphores
    vk::SemaphoreCreateInfo sci{};
    auto imageAvail = device->createSemaphoreUnique(sci);
    auto renderDone = device->createSemaphoreUnique(sci);

    // Main loop
    while(window.isVisible() && clientRunning.load()){
        app.processEvents();
        if(!clientRunning.load())
            window.close();

        std::vector<uint8_t> pixels;
        {
            std::lock_guard<std::mutex> lk(queueMutex);
            if(!frameQueue.empty()){
                pixels = std::move(frameQueue.front());
                frameQueue.pop_front();
            }
        }
        if(pixels.empty()) continue;

        // upload to staging
        void* dst = device->mapMemory(*stagingMem,0,pixels.size());
        std::memcpy(dst, pixels.data(), pixels.size());
        device->unmapMemory(*stagingMem);

        // acquire
        auto [_, imgIndex] = device->acquireNextImageKHR(
            *swapchain, UINT64_MAX,
            *imageAvail, nullptr
        );

        // allocate & record (keep cmdBuffers in scope)
        auto cmdBuffers = device->allocateCommandBuffersUnique({
            *commandPool, vk::CommandBufferLevel::ePrimary, 1
        });
        auto cmdBuf = cmdBuffers.front().get();
        cmdBuf.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

        vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor,0,1,0,1};

        // UNDEFINED -> TRANSFER_DST
        cmdBuf.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer,
            {}, nullptr, nullptr,
            vk::ImageMemoryBarrier{
                {}, vk::AccessFlagBits::eTransferWrite,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                swapImages[imgIndex], range
            }
        );

        // copy buffer->image
        vk::BufferImageCopy bic{
            0,0,0,
            {vk::ImageAspectFlagBits::eColor,0,0,1},
            {0,0,0},
            {WIDTH,HEIGHT,1}
        };
        cmdBuf.copyBufferToImage(
            *stagingBuffer,
            swapImages[imgIndex],
            vk::ImageLayout::eTransferDstOptimal,
            1,&bic
        );

        // TRANSFER_DST -> PRESENT_SRC
        cmdBuf.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, nullptr, nullptr,
            vk::ImageMemoryBarrier{
                vk::AccessFlagBits::eTransferWrite, {},
                vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::ePresentSrcKHR,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                swapImages[imgIndex], range
            }
        );
        cmdBuf.end();

        // submit
        vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eTransfer;
        vk::SubmitInfo si(
            1, &*imageAvail, &waitStage,
            1, &cmdBuf,
            1, &*renderDone
        );
        graphicsQueue.submit(si, nullptr);

        // present
        vk::PresentInfoKHR pi(
            1, &*renderDone,
            1, &*swapchain, &imgIndex
        );
        presentQueue.presentKHR(pi);

        // ensure GPU done before cmdBuffers destroy
        device->waitIdle();
    }

    if(globalSock && globalSock->is_open()){
        boost::system::error_code ec;
        globalSock->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        globalSock->close(ec);
    }
    netThread.join();

    return 0;
}
