#include "application/protocol/tcp_protocol_router.h"

#include <arpa/inet.h>

#include <iostream>
#include <vector>

namespace slg::application::protocol {

TcpProtocolRouter::TcpProtocolRouter(std::shared_ptr<ProtocolRegistry> registry)
    : registry_(std::move(registry)) {}

void TcpProtocolRouter::OnAccept(const slg::network::tcp::TcpConnectionPtr&) {}

void TcpProtocolRouter::OnReceive(const slg::network::tcp::TcpConnectionPtr& connection,
                                  const std::uint8_t* data,
                                  std::size_t size) {
    auto& reader = GetReader(connection.get());
    reader.Feed(data, size, [this, connection](const PacketHeader& header,
                                               std::vector<std::uint8_t> payload) {
        if (!header.ValidateChecksum()) {
            std::cerr << "[protocol] invalid checksum from " << connection->RemoteAddress()
                      << std::endl;
            return;
        }
        if (!registry_->Dispatch(header, connection, payload.data(), payload.size())) {
            std::cerr << "[protocol] unhandled command " << header.command << " from "
                      << connection->RemoteAddress() << std::endl;
        }
    });
}

void TcpProtocolRouter::OnError(const slg::network::tcp::TcpConnectionPtr& connection,
                                const boost::system::error_code&) {
    std::lock_guard<std::mutex> lock(mutex_);
    readers_.erase(connection.get());
}

LengthPrefixedReader& TcpProtocolRouter::GetReader(
    const slg::network::tcp::TcpConnection* connection) {
    std::lock_guard<std::mutex> lock(mutex_);
    return readers_[connection];
}

}  // namespace slg::application::protocol
