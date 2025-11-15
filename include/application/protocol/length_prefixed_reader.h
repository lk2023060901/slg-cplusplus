#pragma once

#include <boost/circular_buffer.hpp>

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include "application/protocol/packet_header.h"

namespace slg::application::protocol {

class LengthPrefixedReader {
public:
    static constexpr std::size_t kDefaultCapacity = 4096;

    LengthPrefixedReader();

    template <typename Callback>
    void Feed(const std::uint8_t* data, std::size_t size, Callback&& callback) {
        Reserve(size);
        buffer_.insert(buffer_.end(), data, data + size);
        while (true) {
            if (!pending_header_) {
                if (buffer_.size() < PacketHeader::kSize) {
                    break;
                }
                std::array<std::uint8_t, PacketHeader::kSize> header_bytes{};
                CopyOut(0, header_bytes.data(), header_bytes.size());
                buffer_.erase_begin(PacketHeader::kSize);
                auto header = DeserializeHeader(header_bytes.data());
                pending_header_ = header;
            }
            if (buffer_.size() < pending_header_->length) {
                break;
            }
            std::vector<std::uint8_t> payload(pending_header_->length);
            if (!payload.empty()) {
                CopyOut(0, payload.data(), payload.size());
                buffer_.erase_begin(payload.size());
            }
            auto header = *pending_header_;
            pending_header_.reset();
            callback(header, std::move(payload));
            if (buffer_.empty()) {
                break;
            }
        }
    }

private:
    void Reserve(std::size_t additional);
    void CopyOut(std::size_t offset, std::uint8_t* dest, std::size_t len) const;

    boost::circular_buffer<std::uint8_t> buffer_;
    std::optional<PacketHeader> pending_header_;
};

inline LengthPrefixedReader::LengthPrefixedReader() : buffer_(kDefaultCapacity) {}

inline void LengthPrefixedReader::Reserve(std::size_t additional) {
    if (buffer_.capacity() - buffer_.size() >= additional) {
        return;
    }
    auto new_capacity = buffer_.capacity();
    while (new_capacity - buffer_.size() < additional) {
        new_capacity = new_capacity == 0 ? kDefaultCapacity : new_capacity * 2;
    }
    boost::circular_buffer<std::uint8_t> new_buffer(new_capacity);
    new_buffer.insert(new_buffer.end(), buffer_.begin(), buffer_.end());
    buffer_.swap(new_buffer);
}

inline void LengthPrefixedReader::CopyOut(std::size_t offset,
                                          std::uint8_t* dest,
                                          std::size_t len) const {
    for (std::size_t i = 0; i < len; ++i) {
        dest[i] = buffer_[offset + i];
    }
}

}  // namespace slg::application::protocol
