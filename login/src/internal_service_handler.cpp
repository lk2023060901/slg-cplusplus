#include "internal_service_handler.h"

#include <chrono>

#include "application/protocol/protocol_registry.h"
#include "application/protocol/security_context.h"
#include "application/protocol/tcp_protocol_router.h"
#include "client/enums.pb.h"
#include "logging_macros.h"
#include "network/tcp/tcp_connection.h"

namespace login {

namespace {

std::uint64_t CurrentTimestampMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

slg::application::protocol::CommandHandler OnInternalHeartbeat(
    const std::shared_ptr<InternalServiceHandler>& handler,
    const std::shared_ptr<slg::application::protocol::SecurityContext>& security_context) {
    return [handler, security_context](const slg::application::protocol::PacketHeader& header,
                                      const slg::network::tcp::TcpConnectionPtr& conn,
                                      const std::uint8_t* data,
                                      std::size_t size) {
        common::HeartbeatReq heartbeat_req;
        if (!heartbeat_req.ParseFromArray(data, static_cast<int>(size))) {
            LOGIN_LOG_WARN("invalid internal heartbeat from {}", conn->RemoteAddress());
            return;
        }
        auto response = handler->HandleHeartbeat(heartbeat_req, conn->RemoteAddress());
        conn->AsyncSend(security_context->EncodeMessage(
            static_cast<std::uint32_t>(client::CMD_HEARTBEAT_RES), response, header.sequence));
    };
}

}  // namespace

common::HeartbeatRes InternalServiceHandler::HandleHeartbeat(
    const common::HeartbeatReq& request,
    const std::string& remote_address) const {
    LOGIN_LOG_DEBUG("internal heartbeat from {}", remote_address);
    common::HeartbeatRes response;
    response.set_client_timestamp(request.client_timestamp());
    response.set_server_timestamp(CurrentTimestampMs());
    return response;
}

void RegisterInternalProtocols(
    const std::shared_ptr<InternalServiceHandler>& handler,
    const std::shared_ptr<slg::application::protocol::SecurityContext>& security_context,
    slg::application::protocol::ProtocolRegistry& registry) {
    registry.Register(static_cast<std::uint32_t>(client::CMD_HEARTBEAT_REQ),
                      OnInternalHeartbeat(handler, security_context));
}

}  // namespace login
