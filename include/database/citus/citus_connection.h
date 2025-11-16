#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/fiber/future.hpp>
#include <libpq-fe.h>

#include "coroutine/scheduler.h"
#include "database/citus/citus_export.h"

namespace slg::database::citus {

class CitusConnection {
public:
    SLG_CITUS_API CitusConnection(boost::asio::io_context& io_context,
                                  slg::coroutine::CoroutineScheduler& scheduler);
    SLG_CITUS_API ~CitusConnection();

    SLG_CITUS_API bool Connect(const std::string& conninfo);
    SLG_CITUS_API void Disconnect();

    SLG_CITUS_API bool Execute(const std::string& sql);
    SLG_CITUS_API std::string ExecuteScalar(const std::string& sql);
    SLG_CITUS_API bool Query(const std::string& sql,
                             const std::function<bool(PGresult*)>& handler);

    SLG_CITUS_API bool IsConnected() const noexcept;

    SLG_CITUS_API PGconn* Handle() const noexcept;

private:
    bool AwaitSocket(boost::asio::posix::stream_descriptor::wait_type type);
    bool FlushOutput();
    bool ConsumeResults(const std::function<bool(PGresult*)>& handler);
    bool SendQuery(const std::string& sql);

    boost::asio::io_context& io_context_;
    slg::coroutine::CoroutineScheduler& scheduler_;
    std::optional<boost::asio::posix::stream_descriptor> socket_;
    PGconn* conn_{nullptr};
};

}  // namespace slg::database::citus
