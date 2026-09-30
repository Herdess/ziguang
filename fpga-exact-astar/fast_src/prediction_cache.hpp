#pragma once

#include "architecture.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace srb {

// Bounded open-addressing cache for delays computed during the current run.
// It stores no Golden labels: a miss is always evaluated by the active model.
class PredictionCache {
public:
    // 2^22 slots with the 70% load limit retain about 2.93 million
    // distinct unseen predictions.  The previous 2^21 table stopped at
    // about 1.46 million entries and repeatedly evaluated evicted-by-limit
    // queries after the official base set was expanded.
    explicit PredictionCache(uint32_t port_count, size_t capacity_power = 22);

    bool lookup(const Pin& source, const Pin& target, uint32_t& delay) const;
    void insert(const Pin& source, const Pin& target, uint32_t delay);
    size_t memory_bytes() const;
    size_t size() const { return size_; }

private:
    uint32_t port_count_ = 0;
    size_t mask_ = 0;
    size_t size_ = 0;
    size_t insertion_limit_ = 0;
    std::vector<uint64_t> keys_;
    std::vector<uint32_t> delays_;

    uint64_t key(const Pin& source, const Pin& target) const;
    static uint64_t hash(uint64_t value);
};

}  // namespace srb
