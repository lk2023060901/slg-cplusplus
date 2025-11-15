#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "compressor/compression_processor.h"
#include "crypto/crypto_processor.h"
#include "application/protocol/message_codec.h"
#include "application/protocol/packet_header.h"

namespace slg::application::protocol {

class SecurityContext {
public:
    SecurityContext(std::shared_ptr<CryptoProcessor> crypto,
                    std::shared_ptr<CompressionProcessor> compression,
                    std::size_t compression_min_bytes = 0);

    bool Decode(const PacketHeader& header, std::vector<std::uint8_t>& payload) const;

    std::vector<std::uint8_t> Encode(std::uint32_t command,
                                     const std::uint8_t* payload,
                                     std::size_t size,
                                     std::uint32_t sequence = 0) const;

    template <typename Message>
    std::vector<std::uint8_t> EncodeMessage(std::uint32_t command,
                                            const Message& message,
                                            std::uint32_t sequence = 0) const {
        const std::string serialized = message.SerializeAsString();
        return Encode(command,
                      reinterpret_cast<const std::uint8_t*>(serialized.data()),
                      serialized.size(), sequence);
    }

private:
    std::shared_ptr<CryptoProcessor> crypto_;
    std::shared_ptr<CompressionProcessor> compression_;
    std::size_t compression_min_bytes_{0};
};

}  // namespace slg::application::protocol
