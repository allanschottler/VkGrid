#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QSet>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <array>
#include <string>
#include <string_view>
#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/log/trivial.hpp>

struct ServerInfo {
    std::string address;
    std::string port;
};

class DiscoveryWindow : public QDialog {
    Q_OBJECT
public:
    explicit DiscoveryWindow(QWidget *parent = nullptr);
    ~DiscoveryWindow();

    ServerInfo selectedServer() const;

private:
    struct DiscoveredServer {
        ServerInfo info;
        std::chrono::steady_clock::time_point last_seen;
    };

    void discoveryThreadFunc();

    QListWidget *m_list;
    QPushButton *m_button;
    QTimer m_timer;

    std::vector<DiscoveredServer> m_discovered;
    mutable std::mutex m_mutex;
    std::atomic_bool m_running{true};
    std::thread m_thread;
};

inline DiscoveryWindow::DiscoveryWindow(QWidget *parent)
    : QDialog(parent), m_list(new QListWidget(this)),
      m_button(new QPushButton("Connect", this)) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_list);
    layout->addWidget(m_button);
    connect(m_button, &QPushButton::clicked, this, &QDialog::accept);

    connect(&m_timer, &QTimer::timeout, [this]() {
        std::lock_guard<std::mutex> lk(m_mutex);
        QSet<QString> current;
        for (const auto &s : m_discovered) {
            QString text = QString::fromStdString(s.info.address + ":" + s.info.port);
            current.insert(text);
            if (m_list->findItems(text, Qt::MatchExactly).isEmpty())
                m_list->addItem(text);
        }
        for (int i = 0; i < m_list->count();) {
            if (!current.contains(m_list->item(i)->text())) {
                delete m_list->takeItem(i);
            } else {
                ++i;
            }
        }
    });
    m_timer.start(500);

    m_thread = std::thread(&DiscoveryWindow::discoveryThreadFunc, this);
}

inline DiscoveryWindow::~DiscoveryWindow() {
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

inline ServerInfo DiscoveryWindow::selectedServer() const {
    int idx = m_list->currentRow();
    if (idx < 0) return {};
    std::lock_guard<std::mutex> lk(m_mutex);
    if (static_cast<size_t>(idx) >= m_discovered.size()) return {};
    return m_discovered[idx].info;
}

inline void DiscoveryWindow::discoveryThreadFunc() {
    using udp = boost::asio::ip::udp;
    boost::asio::io_context ctx;
    udp::socket sock(ctx);
    sock.open(udp::v4());
    sock.set_option(boost::asio::socket_base::reuse_address(true));
    sock.set_option(boost::asio::socket_base::broadcast(true));
    sock.bind(udp::endpoint(udp::v4(), 5001));

    udp::endpoint broadcast_ep(boost::asio::ip::address_v4::broadcast(), 5001);
    auto next_ping = std::chrono::steady_clock::now();
    sock.non_blocking(true);

    while (m_running.load()) {
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
                std::lock_guard<std::mutex> lk(m_mutex);
                bool found = false;
                for (auto &s : m_discovered) {
                    if (s.info.address == sender.address().to_string() && s.info.port == port) {
                        s.last_seen = now;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    m_discovered.push_back({{sender.address().to_string(), port}, now});
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= next_ping) {
            std::string msg = "DISCOVER_VKGRID_SERVER";
            sock.send_to(boost::asio::buffer(msg), broadcast_ep);
            BOOST_LOG_TRIVIAL(info) << "Broadcast ping";
            next_ping = now + std::chrono::seconds(1);
        }

        {
            std::lock_guard<std::mutex> lk(m_mutex);
            auto it = m_discovered.begin();
            while (it != m_discovered.end()) {
                if (now - it->last_seen > std::chrono::seconds(5))
                    it = m_discovered.erase(it);
                else
                    ++it;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
