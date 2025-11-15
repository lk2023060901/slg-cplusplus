#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "application/protocol/length_prefixed_reader.h"
#include "application/protocol/protocol_registry.h"
#include "application/protocol/security_context.h"
#include "network/tcp/tcp_connection.h"
#include <boost/system/error_code.hpp>

namespace slg::application::protocol {

class TcpProtocolRouter {
public:
    TcpProtocolRouter(std::shared_ptr<ProtocolRegistry> registry,
                      std::shared_ptr<SecurityContext> security_context);

    void OnAccept(const slg::network::tcp::TcpConnectionPtr& connection);
    void OnReceive(const slg::network::tcp::TcpConnectionPtr& connection,
                   const std::uint8_t* data,
                   std::size_t size);
    void OnError(const slg::network::tcp::TcpConnectionPtr& connection,
                 const boost::system::error_code& ec);

private:
    LengthPrefixedReader& GetReader(const slg::network::tcp::TcpConnection* connection);

    std::shared_ptr<ProtocolRegistry> registry_;
    std::shared_ptr<SecurityContext> security_context_;
    std::unordered_map<const slg::network::tcp::TcpConnection*, LengthPrefixedReader> readers_;
    std::mutex mutex_;
};

}  // namespace slg::application::protocol
