#include "heuristic.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <utility>

namespace srb {
namespace {

struct NetVariant {
    int16_t dx = 0;
    int16_t dy = 0;
    uint32_t gap_delay = 0;
};

struct RelaxedEdge {
    uint16_t from = 0;
    uint16_t to = 0;
    int16_t dx = 0;
    int16_t dy = 0;
    uint32_t cost = 0;
};

constexpr std::array<std::pair<int32_t, int32_t>, 8> kDirections{{
    {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
}};

uint32_t displacement_key(int dx, int dy) {
    return (static_cast<uint32_t>(static_cast<uint16_t>(dx)) << 16U) |
           static_cast<uint16_t>(dy);
}

}  // namespace

DirectionalPotentialHeuristic::DirectionalPotentialHeuristic(
    const Architecture& architecture) : arch_(architecture) {
    build();
}

size_t DirectionalPotentialHeuristic::potential_index(
    size_t direction, uint16_t target_port, uint16_t internal) const {
    return (direction * arch_.port_count() + target_port) * arch_.internal_count() + internal;
}

void DirectionalPotentialHeuristic::build() {
    const auto start = std::chrono::steady_clock::now();
    const uint32_t site_count = arch_.site_count();
    const uint32_t net_count = arch_.net_count();
    const uint32_t internal_count = arch_.internal_count();
    const uint32_t port_count = arch_.port_count();

    // First collapse all concrete placements of each Net into its distinct
    // (actual dx, actual dy, minimum Gap delay) variants.
    std::vector<std::vector<NetVariant>> variants(net_count);
    for (uint16_t net = 0; net < net_count; ++net) {
        std::unordered_map<uint32_t, uint32_t> best;
        for (uint32_t site = 0; site < site_count; ++site) {
            const Transition& transition = arch_.forward_transition(site, net);
            if (transition.site == kInvalidSite) continue;
            const int dx = static_cast<int>(arch_.site_x(transition.site)) - arch_.site_x(site);
            const int dy = static_cast<int>(arch_.site_y(transition.site)) - arch_.site_y(site);
            const uint32_t cost = static_cast<uint32_t>(transition.block_delay) + transition.line_delay;
            const uint32_t key = displacement_key(dx, dy);
            auto found = best.find(key);
            if (found == best.end() || cost < found->second) best[key] = cost;
        }
        variants[net].reserve(best.size());
        for (const auto& [key, cost] : best) {
            variants[net].push_back({
                static_cast<int16_t>(key >> 16U),
                static_cast<int16_t>(key & 0xffffU),
                cost,
            });
        }
    }

    std::vector<RelaxedEdge> relaxed;
    for (uint16_t from = 0; from < internal_count; ++from) {
        for (const MacroEdge& macro : arch_.macros_from(from)) {
            for (const NetVariant& variant : variants[macro.net]) {
                relaxed.push_back({
                    from, macro.internal, variant.dx, variant.dy,
                    static_cast<uint32_t>(macro.arc_delay) + variant.gap_delay,
                });
            }
        }
    }
    relaxed_edge_count_ = relaxed.size();

    potentials_.assign(
        static_cast<size_t>(kDirectionCount) * port_count * internal_count,
        kPotentialInfinity);

    const size_t matrix_size = static_cast<size_t>(internal_count) * internal_count;
    std::vector<uint32_t> distance(matrix_size);
    std::vector<uint32_t> terminal(internal_count);

    for (size_t direction = 0; direction < kDirectionCount; ++direction) {
        const auto [ux, uy] = kDirections[direction];
        uint32_t scalar = std::numeric_limits<uint32_t>::max();
        for (const RelaxedEdge& edge : relaxed) {
            const int32_t progress = ux * edge.dx + uy * edge.dy;
            if (progress <= 0) continue;
            scalar = std::min<uint32_t>(
                scalar, static_cast<uint32_t>(
                    (static_cast<uint64_t>(kScale) * edge.cost) /
                    static_cast<uint32_t>(progress)));
        }
        if (scalar == std::numeric_limits<uint32_t>::max()) scalar = 0;
        const int32_t ax = static_cast<int32_t>(scalar) * ux;
        const int32_t ay = static_cast<int32_t>(scalar) * uy;
        coefficients_[direction] = {ax, ay};

        std::fill(distance.begin(), distance.end(), kPotentialInfinity);
        for (uint16_t i = 0; i < internal_count; ++i) {
            distance[static_cast<size_t>(i) * internal_count + i] = 0;
        }
        for (const RelaxedEdge& edge : relaxed) {
            const int64_t reduced =
                static_cast<int64_t>(kScale) * edge.cost -
                static_cast<int64_t>(ax) * edge.dx -
                static_cast<int64_t>(ay) * edge.dy;
            if (reduced < 0) {
                throw std::runtime_error("directional reduced edge became negative");
            }
            const size_t index = static_cast<size_t>(edge.from) * internal_count + edge.to;
            distance[index] = std::min<uint32_t>(
                distance[index], static_cast<uint32_t>(reduced));
        }

        // All-pairs shortest paths on the small Internal-Port graph.
        for (uint16_t k = 0; k < internal_count; ++k) {
            for (uint16_t i = 0; i < internal_count; ++i) {
                const uint32_t left = distance[static_cast<size_t>(i) * internal_count + k];
                if (left == kPotentialInfinity) continue;
                for (uint16_t j = 0; j < internal_count; ++j) {
                    const uint32_t right = distance[static_cast<size_t>(k) * internal_count + j];
                    if (right == kPotentialInfinity) continue;
                    const uint64_t candidate = static_cast<uint64_t>(left) + right;
                    uint32_t& current = distance[static_cast<size_t>(i) * internal_count + j];
                    if (candidate < current) current = static_cast<uint32_t>(candidate);
                }
            }
        }

        for (uint16_t target = 0; target < port_count; ++target) {
            std::fill(terminal.begin(), terminal.end(), kPotentialInfinity);
            const int16_t target_internal = arch_.internal_index(target);
            if (target_internal >= 0) terminal[static_cast<uint16_t>(target_internal)] = 0;
            for (const ArcEdge& arc : arch_.arcs_to(target)) {
                const int16_t previous = arch_.internal_index(arc.port);
                if (previous < 0) continue;
                terminal[static_cast<uint16_t>(previous)] = std::min<uint32_t>(
                    terminal[static_cast<uint16_t>(previous)],
                    static_cast<uint32_t>(arc.delay) * kScale);
            }
            for (uint16_t from = 0; from < internal_count; ++from) {
                uint32_t best = kPotentialInfinity;
                for (uint16_t to = 0; to < internal_count; ++to) {
                    const uint32_t path = distance[static_cast<size_t>(from) * internal_count + to];
                    if (path == kPotentialInfinity || terminal[to] == kPotentialInfinity) continue;
                    const uint64_t candidate = static_cast<uint64_t>(path) + terminal[to];
                    if (candidate < best) best = static_cast<uint32_t>(candidate);
                }
                potentials_[potential_index(direction, target, from)] = best;
            }
        }
    }

    std::cerr << "directional_heuristic_ready"
              << " directions=" << kDirectionCount
              << " relaxed_edges=" << relaxed_edge_count_
              << " potential_mib=" << (potentials_.size() * sizeof(uint32_t) / 1048576.0)
              << " init_sec="
              << std::chrono::duration<double>(
                     std::chrono::steady_clock::now() - start).count()
              << '\n';
}

uint32_t DirectionalPotentialHeuristic::estimate(
    uint32_t site, uint16_t internal, const Pin& target) const {
    const auto components = estimate_components(site, internal, target);
    return *std::max_element(components.begin(), components.end());
}

std::array<uint32_t, DirectionalPotentialHeuristic::kDirectionCount>
DirectionalPotentialHeuristic::estimate_components(
    uint32_t site, uint16_t internal, const Pin& target) const {
    const int32_t dx = static_cast<int32_t>(arch_.site_x(target.site)) - arch_.site_x(site);
    const int32_t dy = static_cast<int32_t>(arch_.site_y(target.site)) - arch_.site_y(site);
    std::array<uint32_t, kDirectionCount> result{};
    for (size_t direction = 0; direction < kDirectionCount; ++direction) {
        const uint32_t potential = potentials_[potential_index(direction, target.port, internal)];
        if (potential == kPotentialInfinity) continue;
        const DirectionCoefficient coefficient = coefficients_[direction];
        const int64_t numerator =
            static_cast<int64_t>(coefficient.ax) * dx +
            static_cast<int64_t>(coefficient.ay) * dy + potential;
        if (numerator <= 0) continue;
        // Delays are integral. ceil(numerator / Q) is still admissible and
        // preserves consistency while being one unit stronger than floor.
        const uint64_t value =
            (static_cast<uint64_t>(numerator) + kScale - 1) / kScale;
        result[direction] = static_cast<uint32_t>(value);
    }
    return result;
}

size_t DirectionalPotentialHeuristic::memory_bytes() const {
    return potentials_.capacity() * sizeof(uint32_t);
}

}  // namespace srb
