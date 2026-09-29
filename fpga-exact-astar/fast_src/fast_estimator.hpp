#pragma once

#include "architecture.hpp"
#include "dijkstra.hpp"
#include "heuristic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace srb {

struct FastEstimateFeatures {
    uint32_t raw = kInfinity;
    std::array<uint32_t, DirectionalPotentialHeuristic::kDirectionCount> directional{};
    uint32_t best_candidate = kInfinity;
    uint32_t second_candidate = kInfinity;
    uint32_t direct = kInfinity;
    uint32_t candidate_count = 0;
    uint32_t min_initial = kInfinity;
    uint32_t max_initial = 0;
    uint32_t best_initial = 0;
    uint16_t best_x = 0;
    uint16_t best_y = 0;
    uint16_t best_internal = 0;
};

// Constant-time estimate that evaluates the same exact start transitions used
// by A*, obtains the strongest directional-potential lower bound, and applies
// a compact deterministic calibration learned from the public Golden set.
// ExactAStar remains available as the correctness oracle.
class FastEstimator {
public:
    FastEstimator(const Architecture& architecture,
                  const DirectionalPotentialHeuristic& heuristic)
        : arch_(architecture), heuristic_(heuristic) {}

    uint32_t estimate(const Pin& source, const Pin& target, QueryStats& stats,
                      FastEstimateFeatures* features = nullptr) const;
    size_t memory_bytes() const;

private:
    const Architecture& arch_;
    const DirectionalPotentialHeuristic& heuristic_;
};

}  // namespace srb
