#include "internal_service_handler.h"

#include <chrono>

#include "logging_macros.h"

namespace login {

common::HeartbeatRes InternalServiceHandler::HandleHeartbeat(
    const common::HeartbeatReq& request,
    const std::string& remote_address) const {
    LOGIN_LOG_DEBUG("internal heartbeat from {}", remote_address);
    common::HeartbeatRes response;
    response.set_client_timestamp(request.client_timestamp());
    using namespace std::chrono;
    response.set_server_timestamp(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    return response;
}

}  // namespace login

