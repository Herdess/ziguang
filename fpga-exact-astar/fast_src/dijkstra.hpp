#pragma once

#include "architecture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace srb {

enum class DelayMode {
    Exact,
    RelativeGapEstimate,
};

enum class MarginMode {
    Disabled,
    Fixed,
    Auto,
};

struct MarginConfig {
    MarginMode mode = MarginMode::Disabled;
    uint32_t value = 0;
};

struct QueryStats {
    uint64_t settled_forward = 0;
    uint64_t settled_backward = 0;
    uint64_t relaxed = 0;
    uint64_t bounded_queries = 0;
    uint64_t effective_margin_sum = 0;
    uint32_t effective_margin_max = 0;
    uint64_t heuristic_evaluations = 0;
};

struct HeapItem {
    uint32_t distance = 0;
    uint32_t state = 0;
};

// Monotone integer priority queue. Dijkstra never inserts a key smaller than
// the last key popped from the same queue, allowing uint32 distances to be
// grouped into 33 most-significant-difference buckets.
class RadixHeap {
public:
    void clear();
    bool empty() const { return size_ == 0; }
    const HeapItem& top();
    void push(HeapItem item);
    HeapItem pop();
    size_t capacity_bytes() const;

private:
    std::array<std::vector<HeapItem>, 33> buckets_;
    uint32_t last_ = 0;
    size_t size_ = 0;

    static unsigned bucket_index(uint32_t key, uint32_t last);
    void pull_min_bucket();
};

class BidirectionalDijkstra {
public:
    explicit BidirectionalDijkstra(const Architecture& architecture);
    uint32_t shortest(const Pin& source, const Pin& target, DelayMode mode,
                      MarginConfig margin, QueryStats& stats,
                      std::vector<Pin>* path = nullptr);
    size_t workspace_bytes() const;

private:
    struct SearchBox {
        uint16_t left = 0;
        uint16_t right = 0;
        uint16_t lower = 0;
        uint16_t upper = 0;
    };

    const Architecture& arch_;
    size_t state_count_ = 0;
    std::vector<uint32_t> forward_distance_;
    std::vector<uint32_t> backward_distance_;
    std::vector<uint16_t> forward_stamp_;
    std::vector<uint16_t> backward_stamp_;
    std::vector<uint32_t> forward_parent_;
    std::vector<uint32_t> backward_next_;
    std::vector<uint16_t> forward_parent_net_;
    std::vector<uint16_t> backward_next_net_;
    uint16_t epoch_ = 0;
    RadixHeap forward_heap_;
    RadixHeap backward_heap_;

    template <bool Bounded>
    uint32_t shortest_impl(const Pin& source, const Pin& target, DelayMode mode,
                           const SearchBox& box, QueryStats& stats,
                           std::vector<Pin>* path);
    uint32_t state_id(uint32_t site, uint16_t internal) const;
    uint32_t transition_delay(const Transition& transition, DelayMode mode) const;
    SearchBox make_search_box(const Pin& source, const Pin& target, uint32_t margin) const;
    uint32_t automatic_margin(const Pin& source) const;
    bool inside_box(uint32_t site, const SearchBox& box) const;
    void next_epoch();
    bool has_forward(uint32_t state) const;
    bool has_backward(uint32_t state) const;
    bool set_forward(uint32_t state, uint32_t distance);
    bool set_backward(uint32_t state, uint32_t distance);
    void discard_stale_forward();
    void discard_stale_backward();
};

}  // namespace srb
