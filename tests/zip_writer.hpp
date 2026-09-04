#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace q3::test {

struct ZipEntry {
    std::string name;
    std::string data;
};

inline uint32_t zip_crc32(const void *buf, size_t len) {
    static const auto table = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();

    uint32_t crc = 0xFFFFFFFF;
    const auto *p = static_cast<const uint8_t *>(buf);
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

inline void zip_write16(std::string &out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

inline void zip_write32(std::string &out, uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

inline std::string write_zip(const std::vector<ZipEntry> &entries) {
    std::string out;
    std::vector<uint32_t> local_offsets;
    std::vector<uint32_t> crcs;
    local_offsets.reserve(entries.size());
    crcs.reserve(entries.size());

    // 1. Write local headers and data
    for (const auto &entry : entries) {
        local_offsets.push_back(static_cast<uint32_t>(out.size()));
        uint32_t crc = zip_crc32(entry.data.data(), entry.data.size());
        crcs.push_back(crc);

        zip_write32(out, 0x04034b50);                                    // signature
        zip_write16(out, 20);                                            // version needed
        zip_write16(out, 0);                                             // flags
        zip_write16(out, 0);                                             // method (0 = stored)
        zip_write16(out, 0);                                             // mod time
        zip_write16(out, 0);                                             // mod date
        zip_write32(out, crc);                                           // crc32
        zip_write32(out, static_cast<uint32_t>(entry.data.size()));      // compressed size
        zip_write32(out, static_cast<uint32_t>(entry.data.size()));      // uncompressed size
        zip_write16(out, static_cast<uint16_t>(entry.name.size()));      // name length
        zip_write16(out, 0);                                             // extra field length
        out.append(entry.name);
        out.append(entry.data);
    }

    uint32_t cd_offset = static_cast<uint32_t>(out.size());

    // 2. Write central directory entries
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        zip_write32(out, 0x02014b50);                                    // signature
        zip_write16(out, 20);                                            // version made by
        zip_write16(out, 20);                                            // version needed
        zip_write16(out, 0);                                             // flags
        zip_write16(out, 0);                                             // method
        zip_write16(out, 0);                                             // mod time
        zip_write16(out, 0);                                             // mod date
        zip_write32(out, crcs[i]);                                       // crc32
        zip_write32(out, static_cast<uint32_t>(entry.data.size()));      // compressed size
        zip_write32(out, static_cast<uint32_t>(entry.data.size()));      // uncompressed size
        zip_write16(out, static_cast<uint16_t>(entry.name.size()));      // name length
        zip_write16(out, 0);                                             // extra field length
        zip_write16(out, 0);                                             // comment length
        zip_write16(out, 0);                                             // disk start
        zip_write16(out, 0);                                             // internal attr
        zip_write32(out, 0);                                             // external attr
        zip_write32(out, local_offsets[i]);                              // relative offset
        out.append(entry.name);
    }

    uint32_t cd_size = static_cast<uint32_t>(out.size()) - cd_offset;

    // 3. Write end of central directory record
    zip_write32(out, 0x06054b50);                                        // signature
    zip_write16(out, 0);                                                 // disk num
    zip_write16(out, 0);                                                 // cd start disk
    zip_write16(out, static_cast<uint16_t>(entries.size()));             // records on disk
    zip_write16(out, static_cast<uint16_t>(entries.size()));             // total records
    zip_write32(out, cd_size);                                           // cd size
    zip_write32(out, cd_offset);                                         // cd offset
    zip_write16(out, 0);                                                 // comment length

    return out;
}

} // namespace q3::test
