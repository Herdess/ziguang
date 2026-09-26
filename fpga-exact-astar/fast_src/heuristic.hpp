#pragma once

#include "architecture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace srb {

struct DirectionCoefficient {
    int32_t ax = 0;
    int32_t ay = 0;
};

// Exact A* heuristic built from a translation-relaxed graph. Every real macro
// transition has a no-more-expensive relaxed counterpart, so the resulting
// potential is both admissible and consistent.
class DirectionalPotentialHeuristic {
public:
    static constexpr uint32_t kScale = 256;
    static constexpr size_t kDirectionCount = 8;

    explicit DirectionalPotentialHeuristic(const Architecture& architecture);

    uint32_t estimate(uint32_t site, uint16_t internal, const Pin& target) const;
    const std::array<DirectionCoefficient, kDirectionCount>& coefficients() const {
        return coefficients_;
    }
    size_t relaxed_edge_count() const { return relaxed_edge_count_; }
    size_t memory_bytes() const;

private:
    static constexpr uint32_t kPotentialInfinity = UINT32_MAX / 4;

    const Architecture& arch_;
    std::array<DirectionCoefficient, kDirectionCount> coefficients_{};
    std::vector<uint32_t> potentials_;
    size_t relaxed_edge_count_ = 0;

    size_t potential_index(size_t direction, uint16_t target_port,
                           uint16_t internal) const;
    void build();
};

}  // namespace srb

