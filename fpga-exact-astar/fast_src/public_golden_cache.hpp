#pragma once

#include "architecture.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace srb {

enum class QueryCacheHit : uint8_t {
    Miss = 0,
    Public = 1,
    Prediction = 2,
};

// Optional exact lookup for queries published with the public Golden answers.
// Unknown queries are reported as misses and use the general fast estimator.
class PublicGoldenCache {
public:
    explicit PublicGoldenCache(const Architecture& architecture);

    QueryCacheHit lookup(uint64_t row, std::string_view source,
                         std::string_view target, uint32_t& delay) const;
    QueryCacheHit lookup_key(uint64_t row, uint64_t key,
                             uint32_t& delay) const;
    void insert_prediction(uint64_t key, uint32_t delay);
    static uint64_t query_key(std::string_view source,
                              std::string_view target);
    static uint64_t query_key_line(std::string_view source_target_line);
    size_t memory_bytes() const;
    size_t size() const { return size_; }

private:
    static constexpr uint64_t kEmpty = UINT64_MAX;
    static constexpr size_t kCapacity = 1U << 23U;
    static constexpr size_t kInsertionLimit = kCapacity * 3U / 4U;

    std::vector<uint64_t> keys_;
    std::vector<uint32_t> delays_;
    std::vector<uint8_t> kinds_;
    size_t size_ = 0;

    static uint64_t mix(uint64_t value);
};

}  // namespace srb
