#include "application/protocol/message_codec.h"

#include <arpa/inet.h>

#include "application/protocol/packet_header.h"

namespace slg::application::protocol {

std::vector<std::uint8_t> EncodeCommand(std::uint32_t command,
                                        const std::uint8_t* payload,
                                        std::size_t size,
                                        std::uint16_t flags,
                                        std::uint32_t sequence) {
    PacketHeader header;
    header.command = command;
    header.flags = flags;
    header.length = static_cast<std::uint32_t>(size);
    header.sequence = sequence;
    header.UpdateChecksum();

    std::vector<std::uint8_t> packet(PacketHeader::kSize + size);
    SerializeHeader(header, packet.data());
    if (size > 0) {
        std::memcpy(packet.data() + PacketHeader::kSize, payload, size);
    }
    return packet;
}

std::vector<std::uint8_t> EncodeCommand(std::uint32_t command,
                                        const std::vector<std::uint8_t>& payload,
                                        std::uint16_t flags,
                                        std::uint32_t sequence) {
    return EncodeCommand(command, payload.data(), payload.size(), flags, sequence);
}

std::vector<std::uint8_t> EncodeCommand(std::uint32_t command,
                                        std::vector<std::uint8_t>&& payload,
                                        std::uint16_t flags,
                                        std::uint32_t sequence) {
    return EncodeCommand(command, payload.data(), payload.size(), flags, sequence);
}

}  // namespace slg::application::protocol

