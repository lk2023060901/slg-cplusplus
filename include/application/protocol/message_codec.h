#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <string>

namespace slg::application::protocol {

std::vector<std::uint8_t> EncodeCommand(std::uint32_t command,
                                        const std::uint8_t* payload,
                                        std::size_t size,
                                        std::uint16_t flags = 0,
                                        std::uint32_t sequence = 0);

std::vector<std::uint8_t> EncodeCommand(std::uint32_t command,
                                        const std::vector<std::uint8_t>& payload,
                                        std::uint16_t flags = 0,
                                        std::uint32_t sequence = 0);

std::vector<std::uint8_t> EncodeCommand(std::uint32_t command,
                                        std::vector<std::uint8_t>&& payload,
                                        std::uint16_t flags = 0,
                                        std::uint32_t sequence = 0);

template <typename Message>
std::vector<std::uint8_t> EncodeMessage(std::uint32_t command,
                                        const Message& message,
                                        std::uint16_t flags = 0,
                                        std::uint32_t sequence = 0) {
    const std::string serialized = message.SerializeAsString();
    return EncodeCommand(command,
                         reinterpret_cast<const std::uint8_t*>(serialized.data()),
                         serialized.size(), flags, sequence);
}

}  // namespace slg::application::protocol
