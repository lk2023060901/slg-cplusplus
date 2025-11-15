#include "crypto/aes_crypto_processor.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>

#include <openssl/evp.h>

namespace slg::application::protocol {
namespace {

bool HexCharToValue(char ch, std::uint8_t& value) {
    if (ch >= '0' && ch <= '9') {
        value = static_cast<std::uint8_t>(ch - '0');
        return true;
    }
    if (ch >= 'a' && ch <= 'f') {
        value = static_cast<std::uint8_t>(ch - 'a' + 10);
        return true;
    }
    if (ch >= 'A' && ch <= 'F') {
        value = static_cast<std::uint8_t>(ch - 'A' + 10);
        return true;
    }
    return false;
}

bool ParseHex16(const std::string& input, std::array<std::uint8_t, 16>& out) {
    std::string hex = input;
    if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) {
        hex = hex.substr(2);
    }
    hex.erase(std::remove_if(hex.begin(), hex.end(), ::isspace), hex.end());
    if (hex.size() != 32) {
        return false;
    }
    for (std::size_t i = 0; i < 16; ++i) {
        std::uint8_t high = 0;
        std::uint8_t low = 0;
        if (!HexCharToValue(hex[i * 2], high) || !HexCharToValue(hex[i * 2 + 1], low)) {
            return false;
        }
        out[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

struct EvpCipherCtxDeleter {
    void operator()(EVP_CIPHER_CTX* ctx) const {
        if (ctx != nullptr) {
            EVP_CIPHER_CTX_free(ctx);
        }
    }
};

using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, EvpCipherCtxDeleter>;

}  // namespace

Aes128CtrCryptoProcessor::Aes128CtrCryptoProcessor(const std::string& key_hex,
                                                   const std::string& iv_hex) {
    if (!ParseHex16(key_hex, key_) || !ParseHex16(iv_hex, iv_)) {
        std::cerr << "[crypto] invalid AES-128 key/iv configuration" << std::endl;
        valid_ = false;
        return;
    }
    valid_ = true;
}

bool Aes128CtrCryptoProcessor::Encrypt(std::vector<std::uint8_t>& data) {
    return Process(data, true);
}

bool Aes128CtrCryptoProcessor::Decrypt(std::vector<std::uint8_t>& data) {
    return Process(data, false);
}

bool Aes128CtrCryptoProcessor::Process(std::vector<std::uint8_t>& data, bool encrypt) const {
    if (!valid_) {
        return false;
    }

    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new());
    if (!ctx) {
        return false;
    }
    if (EVP_CipherInit_ex(ctx.get(), EVP_aes_128_ctr(), nullptr, key_.data(), iv_.data(),
                          encrypt ? 1 : 0) != 1) {
        return false;
    }

    std::vector<std::uint8_t> output(data.size());
    int out_len = 0;
    if (EVP_CipherUpdate(ctx.get(), output.data(), &out_len, data.data(),
                         static_cast<int>(data.size())) != 1) {
        return false;
    }
    int final_len = 0;
    if (EVP_CipherFinal_ex(ctx.get(), output.data() + out_len, &final_len) != 1) {
        return false;
    }
    output.resize(static_cast<std::size_t>(out_len + final_len));
    data.swap(output);
    return true;
}

}  // namespace slg::application::protocol

