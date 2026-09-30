#include "prediction_cache.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>

int main() {
    constexpr uint32_t kPortCount = 496;
    constexpr uint32_t kQueries = 2000000;
    srb::PredictionCache cache(kPortCount);
    const srb::Pin target{5000, 1};

    for (uint32_t index = 0; index < kQueries; ++index) {
        const srb::Pin source{index / kPortCount,
                              static_cast<uint16_t>(index % kPortCount)};
        cache.insert(source, target, index % 50000U + 1U);
    }
    if (cache.size() != kQueries) {
        throw std::runtime_error(
            "prediction cache stopped before retaining 2 million queries");
    }

    for (uint32_t index = 0; index < kQueries; ++index) {
        const srb::Pin source{index / kPortCount,
                              static_cast<uint16_t>(index % kPortCount)};
        uint32_t delay = 0;
        if (!cache.lookup(source, target, delay) ||
            delay != index % 50000U + 1U) {
            throw std::runtime_error("prediction cache lookup mismatch");
        }
    }

    std::cout << "prediction_cache_entries=" << cache.size()
              << " memory_mib=" << cache.memory_bytes() / 1048576.0 << '\n';
    return 0;
}
