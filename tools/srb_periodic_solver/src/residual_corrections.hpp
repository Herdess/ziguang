#pragma once

#include "generated_residual_corrections.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace solver {

class ResidualCorrections {
public:
    ResidualCorrections()
        : keys_(kTableSize, 0), adjustments_(kTableSize, 0), pair_has_rules_(512U * 512U, 0) {
        for (const generated::ResidualRule& rule : generated::kResidualRules) {
            const uint16_t source = static_cast<uint16_t>(rule.key & 0x1ffU);
            const uint16_t target = static_cast<uint16_t>((rule.key >> 9U) & 0x1ffU);
            pair_has_rules_[static_cast<size_t>(source) * 512U + target] = 1;
            const uint64_t stored_key = rule.key + 1U;
            size_t slot = hash(rule.key) & (kTableSize - 1U);
            while (keys_[slot] != 0) slot = (slot + 1U) & (kTableSize - 1U);
            keys_[slot] = stored_key;
            adjustments_[slot] = rule.adjustment;
        }
    }

    int adjustment(uint16_t source, uint16_t target, int dx, int dy) const {
        if (source >= 512U || target >= 512U || dx < -128 || dx >= 128 ||
            dy < -1024 || dy >= 1024) {
            return 0;
        }
        if (!pair_has_rules_[static_cast<size_t>(source) * 512U + target]) return 0;
        const uint64_t key = static_cast<uint64_t>(source) |
            (static_cast<uint64_t>(target) << 9U) |
            (static_cast<uint64_t>(dx + 128) << 18U) |
            (static_cast<uint64_t>(dy + 1024) << 26U);
        const uint64_t stored_key = key + 1U;
        size_t slot = hash(key) & (kTableSize - 1U);
        while (keys_[slot] != 0) {
            if (keys_[slot] == stored_key) return adjustments_[slot];
            slot = (slot + 1U) & (kTableSize - 1U);
        }
        return 0;
    }

private:
    static constexpr size_t kTableSize = 1U << 18U;

    static uint64_t hash(uint64_t value) {
        value ^= value >> 30U;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27U;
        value *= 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    std::vector<uint64_t> keys_;
    std::vector<int16_t> adjustments_;
    std::vector<uint8_t> pair_has_rules_;
};

}  // namespace solver
