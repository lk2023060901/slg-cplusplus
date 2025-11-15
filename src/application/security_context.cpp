#include "application/protocol/security_context.h"

namespace slg::application::protocol {

SecurityContext::SecurityContext(std::shared_ptr<CryptoProcessor> crypto,
                                 std::shared_ptr<CompressionProcessor> compression,
                                 std::size_t compression_min_bytes)
    : crypto_(std::move(crypto)),
      compression_(std::move(compression)),
      compression_min_bytes_(compression_min_bytes) {}

bool SecurityContext::Decode(const PacketHeader& header, std::vector<std::uint8_t>& payload) const {
    if (!header.ValidateChecksum()) {
        return false;
    }
    if (header.HasFlag(PacketHeader::kFlagEncrypted)) {
        if (!crypto_->Decrypt(payload)) {
            return false;
        }
    }
    if (header.HasFlag(PacketHeader::kFlagCompressed)) {
        if (!compression_->Decompress(payload)) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> SecurityContext::Encode(std::uint32_t command,
                                                  const std::uint8_t* payload,
                                                  std::size_t size,
                                                  std::uint32_t sequence) const {
    std::vector<std::uint8_t> data(payload, payload + size);
    std::uint16_t flags = 0;
    const bool should_compress = compression_->IsEnabled() &&
                                 (compression_min_bytes_ == 0 ||
                                  data.size() >= compression_min_bytes_);
    if (should_compress) {
        if (!compression_->Compress(data)) {
            return {};
        }
        flags |= PacketHeader::kFlagCompressed;
    }
    if (crypto_->IsEnabled()) {
        if (!crypto_->Encrypt(data)) {
            return {};
        }
        flags |= PacketHeader::kFlagEncrypted;
    }
    return EncodeCommand(command, data.data(), data.size(), flags, sequence);
}

}  // namespace slg::application::protocol
