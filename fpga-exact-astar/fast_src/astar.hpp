#pragma once

#include "architecture.hpp"
#include "dijkstra.hpp"
#include "heuristic.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace srb {

class ExactAStar {
public:
    ExactAStar(const Architecture& architecture,
               const DirectionalPotentialHeuristic& heuristic);

    uint32_t shortest(const Pin& source, const Pin& target, QueryStats& stats,
                      std::vector<Pin>* path = nullptr);
    size_t workspace_bytes() const;

private:
    const Architecture& arch_;
    const DirectionalPotentialHeuristic& heuristic_;
    size_t state_count_ = 0;
    std::vector<uint32_t> distance_;
    std::vector<uint32_t> open_key_;
    std::vector<uint16_t> stamp_;
    std::vector<uint32_t> parent_;
    std::vector<uint16_t> parent_net_;
    uint16_t epoch_ = 0;
    RadixHeap open_;

    uint32_t state_id(uint32_t site, uint16_t internal) const;
    void next_epoch();
    bool has_state(uint32_t state) const;
    bool set_distance(uint32_t state, uint32_t distance);
    void discard_stale();
    uint32_t terminal_cost(uint16_t internal, uint16_t target_port) const;
};

}  // namespace srb
