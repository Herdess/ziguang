#pragma once

#include "grid_format.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace grid16 {

struct AxisDesc {
    uint8_t residue = 0;
    uint8_t lower_node = 0;
    uint8_t upper_node = 0;
    uint8_t reserved = 0;
    uint32_t weight_q16 = 0;
};

inline int canonical_coordinate(int value, int period) {
    if (value >= 0) return value % period;
    return -1 - ((-1 - value) % period);
}

inline uint32_t residue_id(int anchor, int period) {
    return static_cast<uint32_t>(anchor < 0 ? anchor + period : period + anchor);
}

inline int far_coordinate(int anchor, int minimum, int maximum, int period) {
    if (anchor >= 0) return anchor + ((maximum - anchor) / period) * period;
    return anchor - ((anchor - minimum) / period) * period;
}

inline int node_q(int index, int node_count, int q_max) {
    return static_cast<int>(std::llround(
        static_cast<double>(index) * q_max / (node_count - 1)));
}

inline int node_coordinate(int anchor, int far, int period, int node_count, int index) {
    const int sign = far >= anchor ? 1 : -1;
    const int q_max = std::abs((far - anchor) / period);
    return anchor + sign * node_q(index, node_count, q_max) * period;
}

inline AxisDesc make_axis_desc(int value, int minimum, int maximum, int period,
                               int node_count) {
    const int anchor = canonical_coordinate(value, period);
    const int far = far_coordinate(anchor, minimum, maximum, period);
    const int q = std::abs((value - anchor) / period);
    const int q_max = std::abs((far - anchor) / period);
    AxisDesc result;
    result.residue = static_cast<uint8_t>(residue_id(anchor, period));
    for (int index = 0; index + 1 < node_count; ++index) {
        const int lower = node_q(index, node_count, q_max);
        const int upper = node_q(index + 1, node_count, q_max);
        if (q >= lower && q <= upper) {
            result.lower_node = static_cast<uint8_t>(index);
            result.upper_node = static_cast<uint8_t>(index + 1);
            result.weight_q16 = upper == lower ? 0U : static_cast<uint32_t>(
                (static_cast<uint64_t>(q - lower) * 65536U + (upper - lower) / 2U) /
                static_cast<uint32_t>(upper - lower));
            return result;
        }
    }
    throw std::runtime_error("cannot bracket periodic coordinate");
}

inline uint16_t bilinear_q16(uint16_t v00, uint16_t v10, uint16_t v01, uint16_t v11,
                             uint32_t wx, uint32_t wy) {
    if (v00 == kUnreachable || v10 == kUnreachable ||
        v01 == kUnreachable || v11 == kUnreachable) return kUnreachable;
    constexpr uint64_t one = 65536U;
    const uint64_t top = static_cast<uint64_t>(v00) * (one - wx) +
                         static_cast<uint64_t>(v10) * wx;
    const uint64_t bottom = static_cast<uint64_t>(v01) * (one - wx) +
                            static_cast<uint64_t>(v11) * wx;
    const uint64_t combined = top * (one - wy) + bottom * wy;
    return static_cast<uint16_t>((combined + (1ULL << 31U)) >> 32U);
}

}  // namespace grid16
