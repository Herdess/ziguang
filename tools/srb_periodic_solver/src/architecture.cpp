#include "architecture.hpp"

#include "generated_arch_data.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <stdexcept>

namespace solver {

namespace gen = srb::generated;

namespace {

bool parse_int(std::string_view text, int& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

}  // namespace

Architecture::Architecture() : width_(gen::kWidth), height_(gen::kHeight) {
    port_names_.reserve(gen::kPortNames.size());
    for (std::string_view name : gen::kPortNames) {
        const uint16_t id = static_cast<uint16_t>(port_names_.size());
        port_names_.emplace_back(name);
        port_id_.emplace(port_names_.back(), id);
    }
    net_by_from_.assign(port_names_.size(), -1);
    net_by_to_.assign(port_names_.size(), -1);
    is_output_port_.assign(port_names_.size(), 0);
    arcs_from_.assign(port_names_.size(), {});
    for (const gen::ArcRecord& arc : gen::kArcs) {
        is_output_port_.at(arc.to) = 1;
        arcs_from_.at(arc.from).push_back({arc.to, arc.delay});
    }
    nets_.reserve(gen::kNets.size());
    for (const gen::NetRecord& net : gen::kNets) {
        const uint16_t id = static_cast<uint16_t>(nets_.size());
        nets_.push_back({net.from, net.to, net.dx, net.dy});
        net_by_from_.at(net.from) = static_cast<int16_t>(id);
        if (net_by_to_.at(net.to) >= 0) {
            throw std::runtime_error("embedded architecture has a non-unique Net.to port");
        }
        net_by_to_.at(net.to) = static_cast<int16_t>(id);
    }
    for (const gen::GapBlockRecord& block : gen::kGapBlocks) {
        blocks_.push_back({
            block.lower, block.upper, block.left, block.right,
            block.vertical_crossable, block.vertical_delay,
            block.horizontal_crossable, block.horizontal_delay,
        });
    }
    for (const gen::GapLineRecord& line : gen::kGapLines) {
        lines_.push_back({line.direction, line.site, line.delay});
    }
}

NormalizedTarget Architecture::normalize_target_free(const Pin& target) const {
    NormalizedTarget result;
    if (target.x < 0 || target.x >= width_ || target.y < 0 || target.y >= height_) return result;
    if (is_output_port_.at(target.port)) {
        result.matched = true;
        result.x = target.x;
        result.y = target.y;
        result.port = target.port;
        return result;
    }
    const int16_t net_index = net_by_to_.at(target.port);
    if (net_index < 0) return result;
    const NetRule& net = nets_.at(static_cast<uint16_t>(net_index));
    result.x = target.x - net.dx;
    result.y = target.y - net.dy;
    if (result.x < 0 || result.x >= width_ || result.y < 0 || result.y >= height_) return {};
    result.matched = true;
    result.port = net.from;
    result.used_reverse_net = true;
    return result;
}

int32_t Architecture::direct_arc_delay(uint16_t from, uint16_t to) const {
    int32_t best = -1;
    for (const ArcEdge& arc : arcs_from_.at(from)) {
        if (arc.to == to && (best < 0 || arc.delay < best)) best = arc.delay;
    }
    return best;
}

std::vector<SourceCandidate> Architecture::source_candidates(const Pin& source) const {
    std::vector<SourceCandidate> result;
    if (!valid_site(source.x, source.y)) return result;

    const int16_t incoming_net = net_by_to_.at(source.port);
    if (incoming_net >= 0) {
        result.push_back({source.x, source.y, source.port, 0, 0, 0});
        return result;
    }

    auto append_net = [&](uint16_t output_port, uint32_t prefix_delay) {
        const int16_t net_index = net_by_from_.at(output_port);
        if (net_index < 0) return;
        const NetRule& net = nets_.at(static_cast<uint16_t>(net_index));
        const WalkResult walked = walk(source.x, source.y, net);
        if (!walked.valid) return;
        SourceCandidate candidate;
        candidate.x = walked.x;
        candidate.y = walked.y;
        candidate.port = net.to;
        candidate.gap_block_delay = walked.block_delay;
        candidate.gap_line_delay = walked.line_delay;
        candidate.initial_delay = prefix_delay + walked.block_delay + walked.line_delay;
        result.push_back(candidate);
    };

    if (net_by_from_.at(source.port) >= 0) {
        append_net(source.port, 0);
        return result;
    }

    for (const ArcEdge& arc : arcs_from_.at(source.port)) append_net(arc.to, arc.delay);
    return result;
}

std::string_view Architecture::port_name(uint16_t port) const {
    return port_names_.at(port);
}

bool Architecture::parse_pin(std::string_view text, Pin& result) const {
    if (text.size() < 8 || text.substr(0, 4) != "SRB_") return false;
    const size_t first = text.find('_', 4);
    if (first == std::string_view::npos) return false;
    const size_t slash = text.find('/', first + 1);
    if (slash == std::string_view::npos) return false;
    if (!parse_int(text.substr(4, first - 4), result.x) ||
        !parse_int(text.substr(first + 1, slash - first - 1), result.y)) return false;
    auto found = port_id_.find(std::string(text.substr(slash + 1)));
    if (found == port_id_.end()) return false;
    result.port = found->second;
    return true;
}

int Architecture::block_index(int x, int y) const {
    for (size_t index = 0; index < blocks_.size(); ++index) {
        const GapBlock& block = blocks_[index];
        if (x >= block.left && x <= block.right && y >= block.lower && y <= block.upper) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool Architecture::valid_site(int x, int y) const {
    return x >= 0 && x < width_ && y >= 0 && y < height_ && block_index(x, y) < 0;
}

uint32_t Architecture::line_delay(int x0, int y0, int x1, int y1) const {
    uint32_t delay = 0;
    if (x0 != x1) {
        const int lo = std::min(x0, x1);
        const int hi = std::max(x0, x1);
        for (const GapLine& line : lines_) {
            if (line.direction == 1 && lo <= line.site && line.site < hi) delay += line.delay;
        }
    }
    if (y0 != y1) {
        const int lo = std::min(y0, y1);
        const int hi = std::max(y0, y1);
        for (const GapLine& line : lines_) {
            if (line.direction == 0 && lo <= line.site && line.site < hi) delay += line.delay;
        }
    }
    return delay;
}

WalkResult Architecture::walk(int x, int y, const NetRule& net) const {
    WalkResult result;
    result.x = x;
    result.y = y;
    if (!valid_site(x, y) || (net.dx != 0 && net.dy != 0)) return result;

    if (net.dx != 0) {
        const int step = net.dx > 0 ? 1 : -1;
        int remaining = std::abs(static_cast<int>(net.dx));
        while (remaining > 0) {
            const int next = result.x + step;
            if (next < 0 || next >= width_) return result;
            const int index = block_index(next, result.y);
            if (index >= 0 && blocks_[index].horizontal_crossable) {
                const GapBlock& block = blocks_[index];
                result.block_delay += block.horizontal_delay;
                result.x = step > 0 ? block.right : block.left;
                continue;
            }
            result.x = next;
            --remaining;
        }
    } else {
        const int step = net.dy > 0 ? 1 : -1;
        int remaining = std::abs(static_cast<int>(net.dy));
        while (remaining > 0) {
            const int next = result.y + step;
            if (next < 0 || next >= height_) return result;
            const int index = block_index(result.x, next);
            if (index >= 0 && blocks_[index].vertical_crossable) {
                const GapBlock& block = blocks_[index];
                result.block_delay += block.vertical_delay;
                result.y = step > 0 ? block.upper : block.lower;
                continue;
            }
            result.y = next;
            --remaining;
        }
    }
    result.valid = valid_site(result.x, result.y);
    if (result.valid) result.line_delay = line_delay(x, y, result.x, result.y);
    return result;
}

NormalizedSource Architecture::normalize_source(const Pin& source,
                                                uint16_t golden_source_port) const {
    NormalizedSource result;
    if (!valid_site(source.x, source.y)) return result;
    if (source.port == golden_source_port) {
        result.matched = true;
        result.x = source.x;
        result.y = source.y;
        return result;
    }
    const int16_t net_index = net_by_from_.at(source.port);
    if (net_index < 0) return result;
    const NetRule& net = nets_.at(static_cast<uint16_t>(net_index));
    if (net.to != golden_source_port) return result;
    const WalkResult walked = walk(source.x, source.y, net);
    if (!walked.valid) return result;
    result.matched = true;
    result.x = walked.x;
    result.y = walked.y;
    result.initial_gap_delay = walked.block_delay + walked.line_delay;
    result.used_forced_net = true;
    return result;
}

}  // namespace solver
