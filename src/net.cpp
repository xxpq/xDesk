#include <macro.hpp>
#include <net.hpp>
#include <net_adapter_v2.hpp>

namespace net {
namespace {

args::type mode_ = args::type::UNKNOWN;
AdapterV2& adapter = getAdapter();
bool accepted_ = false;

} // namespace

bool start(const std::string& ip, const size_t& port, const args::type mode) {
    mode_ = mode;
    RET_IF(mode == args::type::UNKNOWN, false);

    AdapterConfig cfg;
    cfg.enable_v2 = true;
    cfg.tcp_port = static_cast<uint16_t>(port);
    cfg.kcp_port = static_cast<uint16_t>(port + 1);
    cfg.mode = deskx::net::v2::TransportMode::DUAL_STACK;
    adapter.setConfig(cfg);

    if (mode == args::type::CLIENT) {
        return adapter.initClient(ip, cfg.tcp_port, cfg.kcp_port) && adapter.connect();
    }
    accepted_ = false;
    return adapter.initServer(cfg.tcp_port, cfg.kcp_port);
}

bool connection() {
    RET_IF(mode_ != args::type::SERVER, false);
    if (accepted_) return true;
    accepted_ = adapter.startServer();
    return accepted_;
}

void kick() { adapter.disconnect(); }

status recv(byte* buff, int size) { return adapter.recv(buff, size); }

status send(const byte* buff, int size) { return adapter.send(buff, size); }

void close() {
    accepted_ = false;
    adapter.close();
}

} // namespace net
