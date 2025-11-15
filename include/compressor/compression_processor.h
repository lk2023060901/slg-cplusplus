#pragma once

#include <vector>

namespace slg::application::protocol {

class CompressionProcessor {
public:
    virtual ~CompressionProcessor() = default;

    virtual bool Compress(std::vector<std::uint8_t>& data) = 0;
    virtual bool Decompress(std::vector<std::uint8_t>& data) = 0;
    virtual bool IsEnabled() const { return true; }
};

class NullCompressionProcessor : public CompressionProcessor {
public:
    bool Compress(std::vector<std::uint8_t>&) override { return true; }
    bool Decompress(std::vector<std::uint8_t>&) override { return true; }
    bool IsEnabled() const override { return false; }
};

class Lz4CompressionProcessor : public CompressionProcessor {
public:
    bool Compress(std::vector<std::uint8_t>& data) override;
    bool Decompress(std::vector<std::uint8_t>& data) override;
};

class ZstdCompressionProcessor : public CompressionProcessor {
public:
    explicit ZstdCompressionProcessor(int level = 3);

    bool Compress(std::vector<std::uint8_t>& data) override;
    bool Decompress(std::vector<std::uint8_t>& data) override;

private:
    int level_;
};

}  // namespace slg::application::protocol
