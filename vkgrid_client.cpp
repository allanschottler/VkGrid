#define VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#include <vulkan/vulkan.hpp>

#include "discovery_window.h"
#include "vulkan_window.h"
#include <QApplication>
#include <csignal>
#include <boost/log/trivial.hpp>
#include "logging.h"

int main(int argc, char **argv) {
    init_logging("Client");
    BOOST_LOG_TRIVIAL(info) << "VkGrid Remote Client";

    QApplication app(argc, argv);
    DiscoveryWindow dlg;
    if(dlg.exec() != QDialog::Accepted) return 0;
    ServerInfo info = dlg.selectedServer();
    if(info.address.empty()) return 0;

    VulkanWindow window(info);
    window.run(app);
    return 0;
}
