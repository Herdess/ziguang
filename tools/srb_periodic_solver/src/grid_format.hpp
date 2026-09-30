#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace grid16 {

inline constexpr char kMagic[8] = {'S', 'R', 'B', '4', 'X', '8', '1', '\0'};
inline constexpr uint32_t kVersion = 1;
inline constexpr uint32_t kXPeriod = 10;
inline constexpr uint32_t kYPeriod = 12;
inline constexpr uint32_t kXResidues = 20;
inline constexpr uint32_t kYResidues = 24;
inline constexpr uint32_t kXNodes = 4;
inline constexpr uint32_t kYNodes = 8;
inline constexpr uint32_t kBlockValues = kXNodes * kYNodes;
inline constexpr uint16_t kUnreachable = 0xffffU;
inline constexpr uint64_t kValuesAlignment = 4096;

#pragma pack(push, 1)
struct Header {
    char magic[8]{};
    uint32_t version = kVersion;
    uint32_t header_size = sizeof(Header);
    uint32_t source_count = 0;
    uint32_t target_count = 0;
    int32_t min_dx = -119;
    int32_t max_dx = 119;
    int32_t min_dy = -549;
    int32_t max_dy = 549;
    uint32_t x_period = kXPeriod;
    uint32_t y_period = kYPeriod;
    uint32_t x_residues = kXResidues;
    uint32_t y_residues = kYResidues;
    uint32_t x_nodes = kXNodes;
    uint32_t y_nodes = kYNodes;
    uint32_t value_bits = 16;
    uint32_t reserved0 = 0;
    uint64_t source_ports_offset = 0;
    uint64_t target_ports_offset = 0;
    uint64_t values_offset = 0;
    uint64_t value_count = 0;
    uint64_t values_bytes = 0;
    uint64_t build_runtime_ns = 0;
    uint64_t reserved[17]{};
};
#pragma pack(pop)

static_assert(sizeof(Header) == 256, "grid header must be 256 bytes");

inline uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1U) / alignment * alignment;
}

inline void validate(const Header& h) {
    if (std::memcmp(h.magic, kMagic, sizeof(kMagic)) != 0 || h.version != kVersion ||
        h.header_size != sizeof(Header)) {
        throw std::runtime_error("unsupported 4x8 grid model format");
    }
    if (h.x_period != kXPeriod || h.y_period != kYPeriod ||
        h.x_residues != kXResidues || h.y_residues != kYResidues ||
        h.x_nodes != kXNodes || h.y_nodes != kYNodes || h.value_bits != 16) {
        throw std::runtime_error("incompatible grid geometry");
    }
    const uint64_t expected = static_cast<uint64_t>(h.source_count) * h.target_count *
        kXResidues * kYResidues * kBlockValues;
    if (h.value_count != expected || h.values_bytes != expected * sizeof(uint16_t)) {
        throw std::runtime_error("corrupt grid value count");
    }
}

}  // namespace grid16
