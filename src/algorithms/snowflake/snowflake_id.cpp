#include "algorithms/snowflake/snowflake_id.h"

#include <chrono>
#include <random>
#include <stdexcept>

namespace slg::algorithms {
namespace {
constexpr std::uint64_t kEpoch = 1577836800000ULL;  // 2020-01-01 UTC
constexpr std::uint8_t kDatacenterBits = 5;
constexpr std::uint8_t kWorkerBits = 5;
constexpr std::uint8_t kSequenceBits = 12;

constexpr std::uint16_t kMaxDatacenterId = (1 << kDatacenterBits) - 1;
constexpr std::uint16_t kMaxWorkerId = (1 << kWorkerBits) - 1;
constexpr std::uint16_t kSequenceMask = (1 << kSequenceBits) - 1;

constexpr std::uint8_t kWorkerShift = kSequenceBits;
constexpr std::uint8_t kDatacenterShift = kSequenceBits + kWorkerBits;
constexpr std::uint8_t kTimestampShift = kSequenceBits + kWorkerBits + kDatacenterBits;
}  // namespace

SnowflakeIdGenerator::SnowflakeIdGenerator(std::uint16_t datacenter_id, std::uint16_t worker_id)
    : datacenter_id_(datacenter_id & kMaxDatacenterId),
      worker_id_(worker_id & kMaxWorkerId) {}

std::uint64_t SnowflakeIdGenerator::NextId() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto timestamp = GetCurrentTimestamp();

    if (timestamp < last_timestamp_) {
        timestamp = WaitNextMillis(last_timestamp_);
    }

    if (timestamp != last_timestamp_) {
        sequence_ = MixSequence();
    } else {
        sequence_ = static_cast<std::uint16_t>((sequence_ + 1) & kSequenceMask);
        if (sequence_ == 0) {
            timestamp = WaitNextMillis(last_timestamp_);
        }
    }

    last_timestamp_ = timestamp;

    const std::uint64_t id =
        ((timestamp - kEpoch) << kTimestampShift) |
        (static_cast<std::uint64_t>(datacenter_id_) << kDatacenterShift) |
        (static_cast<std::uint64_t>(worker_id_) << kWorkerShift) |
        sequence_;

    return id;
}

std::uint64_t SnowflakeIdGenerator::GetCurrentTimestamp() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::uint64_t SnowflakeIdGenerator::WaitNextMillis(std::uint64_t current_timestamp) const {
    auto timestamp = GetCurrentTimestamp();
    while (timestamp <= current_timestamp) {
        timestamp = GetCurrentTimestamp();
    }
    return timestamp;
}

std::uint16_t SnowflakeIdGenerator::MixSequence() const {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint16_t> dist(0, kSequenceMask);
    return dist(rng) & kSequenceMask;
}

}  // namespace slg::algorithms
