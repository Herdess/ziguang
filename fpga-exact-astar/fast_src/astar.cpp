#include "astar.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace srb {

ExactAStar::ExactAStar(
    const Architecture& architecture,
    const DirectionalPotentialHeuristic& heuristic)
    : arch_(architecture), heuristic_(heuristic),
      state_count_(static_cast<size_t>(architecture.site_count()) * architecture.internal_count()),
      distance_(state_count_), open_key_(state_count_), stamp_(state_count_, 0) {
    if (state_count_ > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("internal state count exceeds uint32_t");
    }
}

uint32_t ExactAStar::state_id(uint32_t site, uint16_t internal) const {
    return site * arch_.internal_count() + internal;
}

uint32_t ExactAStar::terminal_cost(uint16_t internal, uint16_t target_port) const {
    const uint16_t port = arch_.internal_port(internal);
    uint32_t best = port == target_port ? 0 : kInfinity;
    for (const ArcEdge& arc : arch_.arcs_from(port)) {
        if (arc.port == target_port) best = std::min<uint32_t>(best, arc.delay);
    }
    return best;
}

uint32_t ExactAStar::shortest(
    const Pin& source, const Pin& target, QueryStats& stats,
    std::vector<Pin>* path) {
    next_epoch();
    open_.clear();
    uint32_t best = kInfinity;
    uint32_t goal_state = kInvalidSite;
    std::vector<Pin> direct_path;
    std::unordered_map<uint32_t, std::vector<Pin>> seeds;
    if (path != nullptr && parent_.empty()) {
        parent_.resize(state_count_);
        parent_net_.resize(state_count_);
    }

    if (source.site == target.site && source.port == target.port) {
        best = 0;
        if (path != nullptr) direct_path = {source};
    }
    if (source.site == target.site) {
        for (const ArcEdge& edge : arch_.arcs_from(source.port)) {
            if (edge.port == target.port && edge.delay < best) {
                best = edge.delay;
                if (path != nullptr) direct_path = {source, target};
            }
        }
    }

    auto seed = [&](uint32_t site, uint16_t internal, uint32_t distance,
                    std::vector<Pin> seed_path) {
        const uint32_t state = state_id(site, internal);
        if (!set_distance(state, distance)) return;
        const uint64_t f64 = static_cast<uint64_t>(distance) +
                             heuristic_.estimate(site, internal, target);
        ++stats.heuristic_evaluations;
        const uint32_t f = static_cast<uint32_t>(std::min<uint64_t>(f64, kInfinity));
        open_key_[state] = f;
        open_.push({f, state});
        if (path != nullptr) {
            parent_[state] = kInvalidSite;
            seeds[state] = std::move(seed_path);
        }
    };

    const int16_t source_internal = arch_.internal_index(source.port);
    if (source_internal >= 0) {
        seed(source.site, static_cast<uint16_t>(source_internal), 0, {source});
    }
    for (uint16_t net : arch_.nets_from(source.port)) {
        const Transition& transition = arch_.forward_transition(source.site, net);
        if (transition.site == kInvalidSite) continue;
        const int16_t next_internal = arch_.internal_index_for_net_destination(net);
        const Pin destination{transition.site, arch_.net_rule(net).to_port};
        seed(transition.site, static_cast<uint16_t>(next_internal),
             static_cast<uint32_t>(transition.block_delay) + transition.line_delay,
             {source, destination});
    }
    for (const ArcEdge& arc : arch_.arcs_from(source.port)) {
        for (uint16_t net : arch_.nets_from(arc.port)) {
            const Transition& transition = arch_.forward_transition(source.site, net);
            if (transition.site == kInvalidSite) continue;
            const int16_t next_internal = arch_.internal_index_for_net_destination(net);
            const Pin arc_pin{source.site, arc.port};
            const Pin destination{transition.site, arch_.net_rule(net).to_port};
            seed(transition.site, static_cast<uint16_t>(next_internal),
                 static_cast<uint32_t>(arc.delay) + transition.block_delay + transition.line_delay,
                 {source, arc_pin, destination});
        }
    }

    while (true) {
        discard_stale();
        if (open_.empty() || open_.top().distance >= best) break;
        const HeapItem item = open_.pop();
        const uint32_t g = distance_[item.state];
        const uint32_t site = item.state / arch_.internal_count();
        const uint16_t internal = static_cast<uint16_t>(item.state % arch_.internal_count());
        ++stats.settled_forward;

        if (site == target.site) {
            const uint32_t finish = terminal_cost(internal, target.port);
            if (finish != kInfinity && static_cast<uint64_t>(g) + finish < best) {
                best = g + finish;
                goal_state = item.state;
                direct_path.clear();
            }
        }

        for (const MacroEdge& edge : arch_.macros_from(internal)) {
            const Transition& transition = arch_.forward_transition(site, edge.net);
            if (transition.site == kInvalidSite) continue;
            const uint32_t edge_cost = static_cast<uint32_t>(edge.arc_delay) +
                                       transition.block_delay + transition.line_delay;
            const uint64_t candidate64 = static_cast<uint64_t>(g) + edge_cost;
            if (candidate64 >= best || candidate64 >= kInfinity) continue;
            const uint32_t next = state_id(transition.site, edge.internal);
            const uint32_t candidate = static_cast<uint32_t>(candidate64);
            if (!set_distance(next, candidate)) continue;
            const uint64_t f64 = candidate64 +
                                 heuristic_.estimate(transition.site, edge.internal, target);
            ++stats.heuristic_evaluations;
            const uint32_t f = static_cast<uint32_t>(std::min<uint64_t>(f64, kInfinity));
            open_key_[next] = f;
            open_.push({f, next});
            if (path != nullptr) {
                parent_[next] = item.state;
                parent_net_[next] = edge.net;
            }
            ++stats.relaxed;
        }
    }

    if (path != nullptr) {
        path->clear();
        if (best == kInfinity) return best;
        if (goal_state == kInvalidSite) {
            *path = std::move(direct_path);
            return best;
        }
        auto state_pin = [&](uint32_t state) {
            const uint32_t site = state / arch_.internal_count();
            const uint16_t internal = static_cast<uint16_t>(state % arch_.internal_count());
            return Pin{site, arch_.internal_port(internal)};
        };
        std::vector<uint32_t> chain;
        for (uint32_t state = goal_state;; state = parent_[state]) {
            chain.push_back(state);
            if (parent_[state] == kInvalidSite) break;
        }
        std::reverse(chain.begin(), chain.end());
        const auto seed_path = seeds.find(chain.front());
        if (seed_path == seeds.end()) throw std::runtime_error("missing A* path seed");
        *path = seed_path->second;
        for (size_t i = 1; i < chain.size(); ++i) {
            const uint32_t previous_site = chain[i - 1] / arch_.internal_count();
            const NetRule& net = arch_.net_rule(parent_net_[chain[i]]);
            path->push_back({previous_site, net.from_port});
            path->push_back(state_pin(chain[i]));
        }
        const Pin goal_pin = state_pin(goal_state);
        if (goal_pin.port != target.port || goal_pin.site != target.site) {
            path->push_back(target);
        }
    }
    return best;
}

size_t ExactAStar::workspace_bytes() const {
    return distance_.capacity() * sizeof(uint32_t) +
           open_key_.capacity() * sizeof(uint32_t) +
           stamp_.capacity() * sizeof(uint16_t) +
           parent_.capacity() * sizeof(uint32_t) +
           parent_net_.capacity() * sizeof(uint16_t) +
           open_.capacity_bytes();
}

void ExactAStar::next_epoch() {
    ++epoch_;
    if (epoch_ == 0) {
        std::fill(stamp_.begin(), stamp_.end(), 0);
        epoch_ = 1;
    }
}

bool ExactAStar::has_state(uint32_t state) const {
    return stamp_[state] == epoch_;
}

bool ExactAStar::set_distance(uint32_t state, uint32_t distance) {
    if (!has_state(state) || distance < distance_[state]) {
        stamp_[state] = epoch_;
        distance_[state] = distance;
        return true;
    }
    return false;
}

void ExactAStar::discard_stale() {
    while (!open_.empty()) {
        const HeapItem& item = open_.top();
        if (has_state(item.state) && open_key_[item.state] == item.distance) break;
        open_.pop();
    }
}

}  // namespace srb
