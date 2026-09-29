#include "fast_estimator.hpp"

#include "generated_fast_calibration.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace srb {

uint32_t FastEstimator::estimate(
    const Pin& source, const Pin& target, QueryStats& stats,
    FastEstimateFeatures* features) const {
    FastEstimateFeatures details;
    details.directional.fill(kInfinity);
    if (source.site == target.site && source.port == target.port) {
        details.raw = 0;
        details.best_candidate = 0;
        details.second_candidate = 0;
        details.direct = 0;
        details.min_initial = 0;
        details.directional.fill(0);
        if (features != nullptr) *features = details;
        return 0;
    }
    uint32_t direct = kInfinity;
    uint32_t best = kInfinity;
    uint32_t second = kInfinity;

    if (source.site == target.site) {
        for (const ArcEdge& edge : arch_.arcs_from(source.port)) {
            if (edge.port == target.port) direct = std::min<uint32_t>(direct, edge.delay);
        }
    }

    auto consider = [&](uint32_t site, uint16_t internal, uint32_t initial) {
        const auto components = heuristic_.estimate_components(site, internal, target);
        const uint32_t heuristic = *std::max_element(components.begin(), components.end());
        const uint64_t candidate = static_cast<uint64_t>(initial) + heuristic;
        ++stats.heuristic_evaluations;
        ++details.candidate_count;
        details.min_initial = std::min(details.min_initial, initial);
        details.max_initial = std::max(details.max_initial, initial);
        for (size_t direction = 0; direction < components.size(); ++direction) {
            const uint64_t value = static_cast<uint64_t>(initial) + components[direction];
            details.directional[direction] = std::min<uint32_t>(
                details.directional[direction], static_cast<uint32_t>(value));
        }
        const uint32_t value = static_cast<uint32_t>(candidate);
        if (value < best) {
            second = best;
            best = value;
            details.best_initial = initial;
            details.best_x = arch_.site_x(site);
            details.best_y = arch_.site_y(site);
            details.best_internal = internal;
        } else if (value < second) {
            second = value;
        }
    };

    const int16_t source_internal = arch_.internal_index(source.port);
    if (source_internal >= 0) {
        consider(source.site, static_cast<uint16_t>(source_internal), 0);
    }
    for (uint16_t net : arch_.nets_from(source.port)) {
        const Transition& transition = arch_.forward_transition(source.site, net);
        if (transition.site == kInvalidSite) continue;
        const int16_t next_internal = arch_.internal_index_for_net_destination(net);
        consider(transition.site, static_cast<uint16_t>(next_internal),
                 static_cast<uint32_t>(transition.block_delay) + transition.line_delay);
    }
    for (const ArcEdge& arc : arch_.arcs_from(source.port)) {
        for (uint16_t net : arch_.nets_from(arc.port)) {
            const Transition& transition = arch_.forward_transition(source.site, net);
            if (transition.site == kInvalidSite) continue;
            const int16_t next_internal = arch_.internal_index_for_net_destination(net);
            consider(transition.site, static_cast<uint16_t>(next_internal),
                     static_cast<uint32_t>(arc.delay) + transition.block_delay +
                         transition.line_delay);
        }
    }

    const uint32_t raw = std::min(best, direct);
    details.raw = raw;
    details.best_candidate = best;
    details.second_candidate = second;
    details.direct = direct;
    if (features != nullptr) *features = details;
    if (raw == kInfinity) return raw;

    using namespace fast_calibration;
    uint64_t calibrated =
        (static_cast<uint64_t>(raw) * kScaleNumerator + kScaleDenominator / 2U) /
            kScaleDenominator +
        kIntercept;
    const int delta_x = static_cast<int>(arch_.site_x(target.site)) -
                        arch_.site_x(source.site);
    const int delta_y = static_cast<int>(arch_.site_y(target.site)) -
                        arch_.site_y(source.site);
    const uint32_t dx = static_cast<uint32_t>(std::abs(delta_x));
    const uint32_t dy = static_cast<uint32_t>(std::abs(delta_y));
    const uint32_t sign_x = delta_x < 0 ? 0U : (delta_x > 0 ? 2U : 1U);
    const uint32_t sign_y = delta_y < 0 ? 0U : (delta_y > 0 ? 2U : 1U);
    const uint32_t sign = sign_x * 3U + sign_y;
    const uint32_t delay64 = std::min<uint32_t>(raw / 64U, 127U);
    auto apply = [&](uint16_t factor) {
        calibrated = (calibrated * factor + kQHalf) >> kQShift;
    };

    apply(kFineSpatialQ14[std::min<uint32_t>(dx / 5U, 23U) * 91U +
                            std::min<uint32_t>(dy / 6U, 90U)]);
    apply(kRawDelayQ14[std::min<uint32_t>(raw / 32U, 255U)]);
    apply(kSignQ14[sign]);
    apply(kRemainderQ14[(dx % 10U) * 12U + dy % 12U]);
    apply(kSourcePortQ14[source.port]);
    apply(kTargetPortQ14[target.port]);
    apply(kPortPairQ14[static_cast<uint32_t>(source.port) * kPortCount + target.port]);
    apply(kTargetDelayQ14[static_cast<uint32_t>(target.port) * 128U + delay64]);
    apply(kSourceDelayQ14[static_cast<uint32_t>(source.port) * 128U + delay64]);
    apply(kTargetSignQ14[static_cast<uint32_t>(target.port) * 9U + sign]);
    apply(kSourceSignQ14[static_cast<uint32_t>(source.port) * 9U + sign]);
    const uint32_t signed_x = delta_x < 0
        ? 11U - std::min<uint32_t>(dx / 10U, 11U)
        : 11U + (delta_x > 0 ? std::min<uint32_t>(dx / 10U, 11U) : 0U);
    const uint32_t signed_y = delta_y < 0
        ? 45U - std::min<uint32_t>(dy / 12U, 45U)
        : 45U + (delta_y > 0 ? std::min<uint32_t>(dy / 12U, 45U) : 0U);
    apply(kSignedSpatialQ14[signed_x * 91U + signed_y]);
    const uint32_t answer = static_cast<uint32_t>(
        std::min<uint64_t>(calibrated, std::numeric_limits<uint32_t>::max()));
    return std::min(answer, direct);
}

size_t FastEstimator::memory_bytes() const {
    using namespace fast_calibration;
    return sizeof(kFineSpatialQ14) + sizeof(kRawDelayQ14) + sizeof(kSignQ14) +
           sizeof(kRemainderQ14) + sizeof(kSourcePortQ14) +
           sizeof(kTargetPortQ14) + sizeof(kPortPairQ14) +
           sizeof(kTargetDelayQ14) + sizeof(kSourceDelayQ14) +
           sizeof(kTargetSignQ14) + sizeof(kSourceSignQ14) +
           sizeof(kSignedSpatialQ14);
}

}  // namespace srb
