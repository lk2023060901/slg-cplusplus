#pragma once

#include <cstdint>
#include <mutex>

#include "algorithms/snowflake/snowflake_export.h"

namespace slg::algorithms {

class SLG_SNOWFLAKE_API SnowflakeIdGenerator {
public:
    SnowflakeIdGenerator(std::uint16_t datacenter_id, std::uint16_t worker_id);

    std::uint64_t NextId();

private:
    std::uint64_t GetCurrentTimestamp() const;
    std::uint64_t WaitNextMillis(std::uint64_t current_timestamp) const;
    std::uint16_t MixSequence() const;

    const std::uint16_t datacenter_id_;
    const std::uint16_t worker_id_;
    std::uint64_t last_timestamp_{0};
    std::uint16_t sequence_{0};
    mutable std::mutex mutex_;
};

}  // namespace slg::algorithms
