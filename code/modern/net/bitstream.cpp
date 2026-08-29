#include "bitstream.hpp"

namespace q3::net {

BitWriter::BitWriter(std::size_t initial_capacity_bytes) {
    buffer_.resize(initial_capacity_bytes, 0);
}

void BitWriter::clear() noexcept {
    bit_count_ = 0;
    std::fill(buffer_.begin(), buffer_.end(), 0);
}

void BitWriter::ensure_bit_capacity(std::size_t additional_bits) {
    std::size_t required_bytes = (bit_count_ + additional_bits + 7) / 8;
    if (required_bytes > buffer_.size()) {
        buffer_.resize(std::max(buffer_.size() * 2, required_bytes + 128), 0);
    }
}

void BitWriter::write_bits(uint32_t value, int bits) {
    if (bits <= 0) return;
    ensure_bit_capacity(bits);

    for (int i = 0; i < bits; ++i) {
        int bit = (value >> i) & 1;
        std::size_t byte_index = (bit_count_ + i) / 8;
        std::size_t bit_offset = (bit_count_ + i) % 8;

        if (bit) {
            buffer_[byte_index] |= (1 << bit_offset);
        } else {
            buffer_[byte_index] &= ~(1 << bit_offset);
        }
    }
    bit_count_ += bits;
}

void BitWriter::write_byte(uint8_t value) {
    write_bits(value, 8);
}

void BitWriter::write_short(int16_t value) {
    write_bits(static_cast<uint16_t>(value), 16);
}

void BitWriter::write_int(int32_t value) {
    write_bits(static_cast<uint32_t>(value), 32);
}

void BitWriter::write_float(float value) {
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(float));
    write_bits(raw, 32);
}

void BitWriter::write_string(std::string_view str) {
    for (char c : str) {
        write_byte(static_cast<uint8_t>(c));
    }
    write_byte(0); // Null terminator
}

void BitWriter::write_vec3(const math::Vec3& vec) {
    write_float(vec.x);
    write_float(vec.y);
    write_float(vec.z);
}

void BitWriter::write_data(const void* data, std::size_t bytes) {
    const auto* ptr = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < bytes; ++i) {
        write_byte(ptr[i]);
    }
}

// ---------------------------------------------------------------------------
// BitReader
// ---------------------------------------------------------------------------

BitReader::BitReader(const uint8_t* data, std::size_t byte_count) noexcept
    : data_(data), total_bits_(byte_count * 8), read_bit_(0) {}

BitReader::BitReader(const std::vector<uint8_t>& buffer) noexcept
    : data_(buffer.data()), total_bits_(buffer.size() * 8), read_bit_(0) {}

uint32_t BitReader::read_bits(int bits) {
    if (bits <= 0) return 0;
    uint32_t value = 0;

    for (int i = 0; i < bits; ++i) {
        if (read_bit_ >= total_bits_) {
            break;
        }
        std::size_t byte_index = read_bit_ / 8;
        std::size_t bit_offset = read_bit_ % 8;

        if (data_[byte_index] & (1 << bit_offset)) {
            value |= (1U << i);
        }
        ++read_bit_;
    }
    return value;
}

uint8_t BitReader::read_byte() {
    return static_cast<uint8_t>(read_bits(8));
}

int16_t BitReader::read_short() {
    return static_cast<int16_t>(read_bits(16));
}

int32_t BitReader::read_int() {
    return static_cast<int32_t>(read_bits(32));
}

float BitReader::read_float() {
    uint32_t raw = read_bits(32);
    float val = 0.0f;
    std::memcpy(&val, &raw, sizeof(float));
    return val;
}

std::string BitReader::read_string() {
    std::string result;
    while (has_remaining()) {
        char c = static_cast<char>(read_byte());
        if (c == '\0') break;
        result.push_back(c);
    }
    return result;
}

math::Vec3 BitReader::read_vec3() {
    float x = read_float();
    float y = read_float();
    float z = read_float();
    return {x, y, z};
}

void BitReader::read_data(void* dest, std::size_t bytes) {
    auto* out = static_cast<uint8_t*>(dest);
    for (std::size_t i = 0; i < bytes; ++i) {
        out[i] = read_byte();
    }
}

} // namespace q3::net
