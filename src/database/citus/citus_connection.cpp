#include "database/citus/citus_connection.h"

namespace slg::database::citus {

CitusConnection::CitusConnection() = default;

CitusConnection::~CitusConnection() {
    Disconnect();
}

bool CitusConnection::Connect(const std::string& conninfo) {
    Disconnect();
    conn_ = PQconnectdb(conninfo.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        Disconnect();
        return false;
    }
    return true;
}

void CitusConnection::Disconnect() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool CitusConnection::Execute(const std::string& sql) {
    if (!conn_) {
        return false;
    }
    PGresult* result = PQexec(conn_, sql.c_str());
    if (!result) {
        return false;
    }
    auto status = PQresultStatus(result);
    PQclear(result);
    return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
}

std::string CitusConnection::ExecuteScalar(const std::string& sql) {
    if (!conn_) {
        return {};
    }
    PGresult* result = PQexec(conn_, sql.c_str());
    if (!result) {
        return {};
    }
    std::string value;
    if (PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) > 0 && PQnfields(result) > 0) {
        value = PQgetvalue(result, 0, 0);
    }
    PQclear(result);
    return value;
}

bool CitusConnection::IsConnected() const noexcept {
    return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK;
}

PGconn* CitusConnection::Handle() const noexcept {
    return conn_;
}

}  // namespace slg::database::citus
