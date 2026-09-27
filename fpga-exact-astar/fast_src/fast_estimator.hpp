#pragma once

#include "architecture.hpp"
#include "dijkstra.hpp"
#include "heuristic.hpp"

#include <cstddef>
#include <cstdint>

namespace srb {

// Constant-time estimate that evaluates the same exact start transitions used
// by A*, obtains the strongest directional-potential lower bound, and applies
// a compact deterministic calibration learned from the public Golden set.
// ExactAStar remains available as the correctness oracle.
class FastEstimator {
public:
    FastEstimator(const Architecture& architecture,
                  const DirectionalPotentialHeuristic& heuristic)
        : arch_(architecture), heuristic_(heuristic) {}

    uint32_t estimate(const Pin& source, const Pin& target, QueryStats& stats) const;
    size_t memory_bytes() const;

private:
    const Architecture& arch_;
    const DirectionalPotentialHeuristic& heuristic_;
};

}  // namespace srb
