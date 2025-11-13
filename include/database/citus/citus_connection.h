#pragma once

#include <memory>
#include <string>

#include <libpq-fe.h>

#include "database/citus/citus_export.h"

namespace slg::database::citus {

class CitusConnection {
public:
    SLG_CITUS_API CitusConnection();
    SLG_CITUS_API ~CitusConnection();

    SLG_CITUS_API bool Connect(const std::string& conninfo);
    SLG_CITUS_API void Disconnect();

    SLG_CITUS_API bool Execute(const std::string& sql);
    SLG_CITUS_API std::string ExecuteScalar(const std::string& sql);

    SLG_CITUS_API bool IsConnected() const noexcept;

    SLG_CITUS_API PGconn* Handle() const noexcept;

private:
    PGconn* conn_{nullptr};
};

}  // namespace slg::database::citus
