#include "discovery_window.h"
#include "vulkan_window.h"
#include <QApplication>
#include <csignal>
#include <iostream>

int main(int argc, char **argv) {
    std::cout << "VkGrid Remote Client" << std::endl;
    QApplication app(argc, argv);

    DiscoveryWindow dlg;
    if(dlg.exec() != QDialog::Accepted) return 0;
    ServerInfo info = dlg.selectedServer();
    if(info.address.empty()) return 0;

    VulkanWindow window(info);
    window.run(app);
    return 0;
}
