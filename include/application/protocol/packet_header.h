#pragma once

#include <arpa/inet.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace slg::application::protocol {

struct PacketHeader {
    static constexpr std::size_t kSize = sizeof(std::uint16_t) * 2 + sizeof(std::uint32_t) * 4;
    static constexpr std::uint16_t kCurrentVersion = 1;

    enum Flag : std::uint16_t {
        kFlagEncrypted = 1u << 0,  // 数据已加密
        kFlagCompressed = 1u << 1  // 数据已压缩
    };

    std::uint16_t version{kCurrentVersion};
    std::uint16_t flags{0};
    std::uint32_t command{0};
    std::uint32_t length{0};
    std::uint32_t sequence{0};
    std::uint32_t checksum{0};

    void UpdateChecksum() { checksum = ComputeChecksum(); }
    bool ValidateChecksum() const { return checksum == ComputeChecksum(); }

    void SetFlag(Flag flag) { flags |= flag; }
    void ClearFlag(Flag flag) { flags &= ~flag; }
    bool HasFlag(Flag flag) const { return (flags & flag) != 0; }

private:
    std::uint32_t ComputeChecksum() const {
        return static_cast<std::uint32_t>(version) ^
               (static_cast<std::uint32_t>(flags) << 16) ^ command ^ length ^ sequence;
    }
};

inline void SerializeHeader(const PacketHeader& header, std::uint8_t* out) {
    std::uint16_t version = htons(header.version);
    std::uint16_t flags = htons(header.flags);
    std::uint32_t command = htonl(header.command);
    std::uint32_t length = htonl(header.length);
    std::uint32_t sequence = htonl(header.sequence);
    std::uint32_t checksum = htonl(header.checksum);

    std::memcpy(out, &version, sizeof(version));
    out += sizeof(version);
    std::memcpy(out, &flags, sizeof(flags));
    out += sizeof(flags);
    std::memcpy(out, &command, sizeof(command));
    out += sizeof(command);
    std::memcpy(out, &length, sizeof(length));
    out += sizeof(length);
    std::memcpy(out, &sequence, sizeof(sequence));
    out += sizeof(sequence);
    std::memcpy(out, &checksum, sizeof(checksum));
}

inline PacketHeader DeserializeHeader(const std::uint8_t* data) {
    PacketHeader header;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint32_t command = 0;
    std::uint32_t length = 0;
    std::uint32_t sequence = 0;
    std::uint32_t checksum = 0;

    std::memcpy(&version, data, sizeof(version));
    data += sizeof(version);
    std::memcpy(&flags, data, sizeof(flags));
    data += sizeof(flags);
    std::memcpy(&command, data, sizeof(command));
    data += sizeof(command);
    std::memcpy(&length, data, sizeof(length));
    data += sizeof(length);
    std::memcpy(&sequence, data, sizeof(sequence));
    data += sizeof(sequence);
    std::memcpy(&checksum, data, sizeof(checksum));

    header.version = ntohs(version);
    header.flags = ntohs(flags);
    header.command = ntohl(command);
    header.length = ntohl(length);
    header.sequence = ntohl(sequence);
    header.checksum = ntohl(checksum);
    return header;
}

}  // namespace slg::application::protocol
