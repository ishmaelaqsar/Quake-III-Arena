#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include "../math/vec3.hpp"

namespace q3::net {

class BitWriter {
public:
    explicit BitWriter(std::size_t initial_capacity_bytes = 1024);

    void write_bits(uint32_t value, int bits);
    void write_byte(uint8_t value);
    void write_short(int16_t value);
    void write_int(int32_t value);
    void write_float(float value);
    void write_string(std::string_view str);
    void write_vec3(const math::Vec3& vec);
    void write_data(const void* data, std::size_t bytes);

    const uint8_t* data() const noexcept { return buffer_.data(); }
    std::size_t byte_size() const noexcept { return (bit_count_ + 7) / 8; }
    std::size_t bit_size() const noexcept { return bit_count_; }

    const std::vector<uint8_t>& buffer() const noexcept { return buffer_; }
    void clear() noexcept;

private:
    void ensure_bit_capacity(std::size_t additional_bits);

    std::vector<uint8_t> buffer_;
    std::size_t bit_count_{0};
};

class BitReader {
public:
    BitReader(const uint8_t* data, std::size_t byte_count) noexcept;
    explicit BitReader(const std::vector<uint8_t>& buffer) noexcept;

    uint32_t read_bits(int bits);
    uint8_t read_byte();
    int16_t read_short();
    int32_t read_int();
    float read_float();
    std::string read_string();
    math::Vec3 read_vec3();
    void read_data(void* dest, std::size_t bytes);

    bool has_remaining() const noexcept { return read_bit_ < total_bits_; }
    std::size_t remaining_bits() const noexcept {
        return total_bits_ > read_bit_ ? total_bits_ - read_bit_ : 0;
    }
    std::size_t current_bit() const noexcept { return read_bit_; }

private:
    const uint8_t* data_{nullptr};
    std::size_t total_bits_{0};
    std::size_t read_bit_{0};
};

} // namespace q3::net
