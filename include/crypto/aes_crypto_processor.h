#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "crypto/crypto_processor.h"

namespace slg::application::protocol {

class Aes128CtrCryptoProcessor : public CryptoProcessor {
public:
    Aes128CtrCryptoProcessor(const std::string& key_hex, const std::string& iv_hex);

    bool Encrypt(std::vector<std::uint8_t>& data) override;
    bool Decrypt(std::vector<std::uint8_t>& data) override;
    bool IsEnabled() const override { return valid_; }

private:
    bool Process(std::vector<std::uint8_t>& data, bool encrypt) const;

    std::array<std::uint8_t, 16> key_{};
    std::array<std::uint8_t, 16> iv_{};
    bool valid_{false};
};

}  // namespace slg::application::protocol

