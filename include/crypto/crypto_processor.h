#pragma once

#include <vector>

namespace slg::application::protocol {

class CryptoProcessor {
public:
    virtual ~CryptoProcessor() = default;

    virtual bool Encrypt(std::vector<std::uint8_t>& data) = 0;
    virtual bool Decrypt(std::vector<std::uint8_t>& data) = 0;
    virtual bool IsEnabled() const { return true; }
};

class NullCryptoProcessor : public CryptoProcessor {
public:
    bool Encrypt(std::vector<std::uint8_t>&) override { return true; }
    bool Decrypt(std::vector<std::uint8_t>&) override { return true; }
    bool IsEnabled() const override { return false; }
};

}  // namespace slg::application::protocol

