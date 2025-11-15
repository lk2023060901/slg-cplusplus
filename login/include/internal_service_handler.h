#pragma once

#include <string>
#include <string_view>

#include "common/common.pb.h"

namespace login {

class InternalServiceHandler {
public:
    InternalServiceHandler() = default;
    ~InternalServiceHandler() = default;

    common::HeartbeatRes HandleHeartbeat(const common::HeartbeatReq& request,
                                         const std::string& remote_address) const;
};

}  // namespace login
