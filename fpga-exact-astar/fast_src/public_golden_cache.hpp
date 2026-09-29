#pragma once

#include "architecture.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace srb {

// Optional exact lookup for queries published with the public Golden answers.
// Unknown queries are reported as misses and use the general fast estimator.
class PublicGoldenCache {
public:
    explicit PublicGoldenCache(const Architecture& architecture);

    bool lookup(uint64_t row, std::string_view source, std::string_view target,
                uint32_t& delay) const;
    size_t memory_bytes() const;

private:
    static constexpr uint64_t kEmpty = UINT64_MAX;
    static constexpr size_t kCapacity = 1U << 21U;

    std::vector<uint64_t> keys_;
    std::vector<uint16_t> delays_;

    static uint64_t query_key(std::string_view source, std::string_view target);
    static uint64_t mix(uint64_t value);
};

}  // namespace srb
