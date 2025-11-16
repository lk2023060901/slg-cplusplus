#include "database/citus/citus_connection.h"

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

namespace slg::database::citus {

CitusConnection::CitusConnection(boost::asio::io_context& io_context,
                                 slg::coroutine::CoroutineScheduler& scheduler)
    : io_context_(io_context), scheduler_(scheduler) {}

CitusConnection::~CitusConnection() {
    Disconnect();
}

bool CitusConnection::Connect(const std::string& conninfo) {
    Disconnect();

    conn_ = PQconnectStart(conninfo.c_str());
    if (!conn_) {
        return false;
    }

    if (PQsetnonblocking(conn_, 1) != 0) {
        Disconnect();
        return false;
    }

    const int fd = PQsocket(conn_);
    if (fd < 0) {
        Disconnect();
        return false;
    }

    socket_.emplace(io_context_, fd);

    while (true) {
        auto status = PQconnectPoll(conn_);
        if (status == PGRES_POLLING_FAILED) {
            Disconnect();
            return false;
        }
        if (status == PGRES_POLLING_OK) {
            return true;
        }
        if (status == PGRES_POLLING_READING) {
            if (!AwaitSocket(boost::asio::posix::stream_descriptor::wait_read)) {
                Disconnect();
                return false;
            }
        } else if (status == PGRES_POLLING_WRITING) {
            if (!AwaitSocket(boost::asio::posix::stream_descriptor::wait_write)) {
                Disconnect();
                return false;
            }
        }
    }
}

void CitusConnection::Disconnect() {
    if (socket_.has_value()) {
        socket_->release();
        socket_.reset();
    }
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool CitusConnection::Execute(const std::string& sql) {
    return Query(sql, [](PGresult* result) {
        auto status = PQresultStatus(result);
        return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
    });
}

std::string CitusConnection::ExecuteScalar(const std::string& sql) {
    std::string value;
    if (!Query(sql, [&value](PGresult* result) {
            if (PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) > 0 &&
                PQnfields(result) > 0) {
                value = PQgetvalue(result, 0, 0);
            }
            return true;
        })) {
        return {};
    }
    return value;
}

bool CitusConnection::Query(const std::string& sql,
                            const std::function<bool(PGresult*)>& handler) {
    if (!conn_) {
        return false;
    }
    if (!SendQuery(sql)) {
        return false;
    }
    return ConsumeResults(handler);
}

bool CitusConnection::IsConnected() const noexcept {
    return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK;
}

PGconn* CitusConnection::Handle() const noexcept {
    return conn_;
}

bool CitusConnection::AwaitSocket(boost::asio::posix::stream_descriptor::wait_type type) {
    if (!socket_) {
        return false;
    }
    auto promise = std::make_shared<boost::fibers::promise<void>>();
    auto future = promise->get_future();

    socket_->async_wait(type, [this, promise](const boost::system::error_code& ec) mutable {
        scheduler_.Schedule([promise, ec]() mutable {
            if (ec) {
                promise->set_exception(std::make_exception_ptr(boost::system::system_error(ec)));
            } else {
                promise->set_value();
            }
        });
    });

    try {
        future.get();
        return true;
    } catch (...) {
        return false;
    }
}

bool CitusConnection::FlushOutput() {
    while (true) {
        const int flush_status = PQflush(conn_);
        if (flush_status == 0) {
            return true;
        }
        if (flush_status < 0) {
            return false;
        }
        if (!AwaitSocket(boost::asio::posix::stream_descriptor::wait_write)) {
            return false;
        }
    }
}

bool CitusConnection::ConsumeResults(const std::function<bool(PGresult*)>& handler) {
    while (true) {
        if (PQconsumeInput(conn_) == 0) {
            return false;
        }

        while (!PQisBusy(conn_)) {
            PGresult* result = PQgetResult(conn_);
            if (!result) {
                return true;
            }

            const bool keep_running = handler(result);
            PQclear(result);
            if (!keep_running) {
                return false;
            }
        }

        if (!AwaitSocket(boost::asio::posix::stream_descriptor::wait_read)) {
            return false;
        }
    }
}

bool CitusConnection::SendQuery(const std::string& sql) {
    if (PQsendQuery(conn_, sql.c_str()) == 0) {
        return false;
    }
    return FlushOutput();
}

}  // namespace slg::database::citus
