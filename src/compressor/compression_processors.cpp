#include "compressor/compression_processor.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cstring>

#include "lz4.h"
#include "zstd.h"

namespace slg::application::protocol {

bool Lz4CompressionProcessor::Compress(std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return true;
    }
    const int max_size = LZ4_compressBound(static_cast<int>(data.size()));
    std::vector<std::uint8_t> output(sizeof(std::uint32_t) + static_cast<std::size_t>(max_size));
    const std::uint32_t original_size = htonl(static_cast<std::uint32_t>(data.size()));
    std::memcpy(output.data(), &original_size, sizeof(original_size));
    const int result = LZ4_compress_default(
        reinterpret_cast<const char*>(data.data()),
        reinterpret_cast<char*>(output.data() + sizeof(std::uint32_t)),
        static_cast<int>(data.size()), max_size);
    if (result <= 0) {
        return false;
    }
    output.resize(sizeof(std::uint32_t) + static_cast<std::size_t>(result));
    data.swap(output);
    return true;
}

bool Lz4CompressionProcessor::Decompress(std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return true;
    }
    // Caller must know the expected decompressed size; here we assume it's stored at start.
    if (data.size() < sizeof(std::uint32_t)) {
        return false;
    }
    std::uint32_t decompressed_size = 0;
    std::memcpy(&decompressed_size, data.data(), sizeof(decompressed_size));
    decompressed_size = ntohl(decompressed_size);
    std::vector<std::uint8_t> output(decompressed_size);
    const int result = LZ4_decompress_safe(
        reinterpret_cast<const char*>(data.data() + sizeof(std::uint32_t)),
        reinterpret_cast<char*>(output.data()),
        static_cast<int>(data.size() - sizeof(std::uint32_t)),
        static_cast<int>(decompressed_size));
    if (result < 0) {
        return false;
    }
    data.swap(output);
    return true;
}

ZstdCompressionProcessor::ZstdCompressionProcessor(int level) : level_(level) {}

bool ZstdCompressionProcessor::Compress(std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return true;
    }
    const size_t bound = ZSTD_compressBound(data.size());
    std::vector<std::uint8_t> output(bound + sizeof(std::uint32_t));
    const std::uint32_t original_size = htonl(static_cast<std::uint32_t>(data.size()));
    std::memcpy(output.data(), &original_size, sizeof(original_size));
    const size_t result = ZSTD_compress(output.data() + sizeof(std::uint32_t), bound,
                                        data.data(), data.size(), level_);
    if (ZSTD_isError(result)) {
        return false;
    }
    output.resize(sizeof(std::uint32_t) + result);
    data.swap(output);
    return true;
}

bool ZstdCompressionProcessor::Decompress(std::vector<std::uint8_t>& data) {
    if (data.size() < sizeof(std::uint32_t)) {
        return false;
    }
    std::uint32_t decompressed_size = 0;
    std::memcpy(&decompressed_size, data.data(), sizeof(decompressed_size));
    decompressed_size = ntohl(decompressed_size);
    std::vector<std::uint8_t> output(decompressed_size);
    const size_t result = ZSTD_decompress(output.data(), decompressed_size,
                                          data.data() + sizeof(std::uint32_t),
                                          data.size() - sizeof(std::uint32_t));
    if (ZSTD_isError(result)) {
        return false;
    }
    output.resize(result);
    data.swap(output);
    return true;
}

}  // namespace slg::application::protocol
