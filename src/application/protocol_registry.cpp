#include "application/protocol/protocol_registry.h"

namespace slg::application::protocol {

void ProtocolRegistry::Register(std::uint32_t command, CommandHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[command] = std::move(handler);
}

bool ProtocolRegistry::Dispatch(const PacketHeader& header,
                                const slg::network::tcp::TcpConnectionPtr& connection,
                                const std::uint8_t* payload,
                                std::size_t size) const {
    CommandHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = handlers_.find(header.command);
        if (iter == handlers_.end()) {
            return false;
        }
        handler = iter->second;
    }
    handler(header, connection, payload, size);
    return true;
}

}  // namespace slg::application::protocol
