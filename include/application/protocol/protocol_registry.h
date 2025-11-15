#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "application/protocol/packet_header.h"
#include "network/tcp/tcp_connection.h"

namespace slg::application::protocol {

using CommandHandler = std::function<void(const PacketHeader&,
                                          const slg::network::tcp::TcpConnectionPtr&,
                                          const std::uint8_t*,
                                          std::size_t)>;

class ProtocolRegistry {
public:
    void Register(std::uint32_t command, CommandHandler handler);
    bool Dispatch(const PacketHeader& header,
                  const slg::network::tcp::TcpConnectionPtr& connection,
                  const std::uint8_t* payload,
                  std::size_t size) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, CommandHandler> handlers_;
};

}  // namespace slg::application::protocol
