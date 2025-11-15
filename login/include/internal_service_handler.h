#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "common/common.pb.h"

namespace slg::application::protocol {
class SecurityContext;
class ProtocolRegistry;
class TcpProtocolRouter;
}  // namespace slg::application::protocol

namespace login {

class InternalServiceHandler {
public:
    InternalServiceHandler() = default;
    ~InternalServiceHandler() = default;

    common::HeartbeatRes HandleHeartbeat(const common::HeartbeatReq& request,
                                         const std::string& remote_address) const;
};

void RegisterInternalProtocols(
    const std::shared_ptr<InternalServiceHandler>& handler,
    const std::shared_ptr<slg::application::protocol::SecurityContext>& security_context,
    slg::application::protocol::ProtocolRegistry& registry);

}  // namespace login
