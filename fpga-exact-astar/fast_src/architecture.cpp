#include "architecture.hpp"

#include "generated_arch_data.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace srb {

Architecture::Architecture() { build(); }

Pin Architecture::parse_pin(std::string_view text) const {
    if (text.rfind("SRB_", 0) != 0) throw std::runtime_error("bad pin: " + std::string(text));
    const size_t x_begin = 4;
    const size_t x_end = text.find('_', x_begin);
    const size_t y_end = x_end == std::string_view::npos ? x_end : text.find('/', x_end + 1);
    if (x_end == std::string_view::npos || y_end == std::string_view::npos || y_end + 1 >= text.size()) {
        throw std::runtime_error("bad pin: " + std::string(text));
    }

    int x = 0;
    int y = 0;
    try {
        x = std::stoi(std::string(text.substr(x_begin, x_end - x_begin)));
        y = std::stoi(std::string(text.substr(x_end + 1, y_end - x_end - 1)));
    } catch (const std::exception&) {
        throw std::runtime_error("bad pin coordinate: " + std::string(text));
    }
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        throw std::runtime_error("pin outside SRB array: " + std::string(text));
    }
    const int32_t compact = physical_to_compact_[physical_site(x, y)];
    if (compact < 0) throw std::runtime_error("pin is on a Gap Block site: " + std::string(text));

    const std::string port(text.substr(y_end + 1));
    auto found = port_id_.find(port);
    if (found == port_id_.end()) throw std::runtime_error("unknown port in pin: " + std::string(text));
    return {static_cast<uint32_t>(compact), found->second};
}

std::string Architecture::format_pin(const Pin& pin) const {
    if (pin.site >= site_x_.size() || pin.port >= port_names_.size()) {
        throw std::runtime_error("invalid pin id while formatting path");
    }
    return "SRB_" + std::to_string(site_x_[pin.site]) + "_" +
           std::to_string(site_y_[pin.site]) + "/" + port_names_[pin.port];
}

int16_t Architecture::internal_index_for_net_destination(uint16_t net) const {
    return internal_index_by_port_[nets_[net].to_port];
}

const Transition& Architecture::forward_transition(uint32_t site, uint16_t net) const {
    return forward_transitions_[static_cast<size_t>(site) * nets_.size() + net];
}

const Transition& Architecture::reverse_transition(uint32_t site, uint16_t net) const {
    return reverse_transitions_[static_cast<size_t>(site) * nets_.size() + net];
}

uint32_t Architecture::line_delay(int x0, int y0, int x1, int y1) const {
    uint32_t delay = 0;
    if (x0 != x1) {
        const int lo = std::min(x0, x1);
        const int hi = std::max(x0, x1);
        delay += vertical_prefix_[hi] - vertical_prefix_[lo];
    }
    if (y0 != y1) {
        const int lo = std::min(y0, y1);
        const int hi = std::max(y0, y1);
        delay += horizontal_prefix_[hi] - horizontal_prefix_[lo];
    }
    return delay;
}

uint32_t Architecture::relative_line_delay(uint32_t from_site, uint32_t to_site) const {
    return line_delay(site_x_[from_site], site_y_[from_site], site_x_[to_site], site_y_[to_site]);
}

Transition Architecture::walk(uint32_t compact_site, const NetRule& net) const {
    int x = site_x_[compact_site];
    int y = site_y_[compact_site];
    const int start_x = x;
    const int start_y = y;
    uint32_t block_delay = 0;

    if (net.dx != 0) {
        const int step = net.dx > 0 ? 1 : -1;
        int remaining = std::abs(static_cast<int>(net.dx));
        while (remaining > 0) {
            const int next = x + step;
            if (next < 0 || next >= width_) return {};
            const int16_t block_index = block_at_[physical_site(next, y)];
            if (block_index >= 0 && blocks_[block_index].horizontal_crossable) {
                const GapBlock& block = blocks_[block_index];
                block_delay += block.horizontal_delay;
                x = step > 0 ? block.right : block.left;
                continue;
            }
            // In an uncrossable direction Block coordinates still consume
            // nominal Net span.  Only a final landing inside the Block breaks
            // the Net, which is checked by physical_to_compact_ below.
            x = next;
            --remaining;
        }
    } else {
        const int step = net.dy > 0 ? 1 : -1;
        int remaining = std::abs(static_cast<int>(net.dy));
        while (remaining > 0) {
            const int next = y + step;
            if (next < 0 || next >= height_) return {};
            const int16_t block_index = block_at_[physical_site(x, next)];
            if (block_index >= 0 && blocks_[block_index].vertical_crossable) {
                const GapBlock& block = blocks_[block_index];
                block_delay += block.vertical_delay;
                y = step > 0 ? block.upper : block.lower;
                continue;
            }
            y = next;
            --remaining;
        }
    }

    const int32_t destination = physical_to_compact_[physical_site(x, y)];
    if (destination < 0) return {};
    const uint32_t gap_line_delay = line_delay(start_x, start_y, x, y);
    if (block_delay > UINT16_MAX || gap_line_delay > UINT16_MAX) {
        throw std::runtime_error("single Net Gap delay exceeds uint16_t");
    }
    return {
        static_cast<uint32_t>(destination),
        static_cast<uint16_t>(block_delay),
        static_cast<uint16_t>(gap_line_delay),
    };
}

Transition Architecture::reverse_walk(uint32_t destination, const NetRule& net) const {
    NetRule reverse = net;
    reverse.dx = static_cast<int16_t>(-reverse.dx);
    reverse.dy = static_cast<int16_t>(-reverse.dy);
    const Transition candidate = walk(destination, reverse);
    if (candidate.site == kInvalidSite) return {};
    const Transition check = walk(candidate.site, net);
    if (check.site != destination) return {};
    return {candidate.site, check.block_delay, check.line_delay};
}

size_t Architecture::static_memory_bytes() const {
    return forward_transitions_.capacity() * sizeof(Transition) +
           reverse_transitions_.capacity() * sizeof(Transition) +
           physical_to_compact_.capacity() * sizeof(int32_t) +
           block_at_.capacity() * sizeof(int16_t);
}

void Architecture::build() {
    const auto start = std::chrono::steady_clock::now();
    width_ = generated::kWidth;
    height_ = generated::kHeight;

    blocks_.reserve(generated::kGapBlocks.size());
    block_at_.assign(static_cast<size_t>(width_) * height_, -1);
    for (const generated::GapBlockRecord& item : generated::kGapBlocks) {
        blocks_.push_back({
            item.lower, item.upper, item.left, item.right,
            item.vertical_crossable, item.vertical_delay,
            item.horizontal_crossable, item.horizontal_delay,
        });
    }
    for (uint16_t index = 0; index < blocks_.size(); ++index) {
        const GapBlock& block = blocks_[index];
        for (int y = block.lower; y <= block.upper; ++y) {
            for (int x = block.left; x <= block.right; ++x) {
                block_at_[physical_site(x, y)] = static_cast<int16_t>(index);
            }
        }
    }

    // The generator verifies that SRB_Inst equals the fixed rectangle minus
    // these Gap Blocks, so no instance JSON is needed at runtime.
    physical_to_compact_.assign(static_cast<size_t>(width_) * height_, -1);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (block_at_[physical_site(x, y)] >= 0) continue;
            physical_to_compact_[physical_site(x, y)] = static_cast<int32_t>(site_x_.size());
            site_x_.push_back(static_cast<uint16_t>(x));
            site_y_.push_back(static_cast<uint16_t>(y));
        }
    }

    vertical_prefix_.assign(static_cast<size_t>(width_) + 1, 0);
    horizontal_prefix_.assign(static_cast<size_t>(height_) + 1, 0);
    for (const generated::GapLineRecord& line : generated::kGapLines) {
        if (line.direction == 1) {
            vertical_prefix_[static_cast<size_t>(line.site) + 1] += line.delay;
        } else {
            horizontal_prefix_[static_cast<size_t>(line.site) + 1] += line.delay;
        }
    }
    for (size_t i = 1; i < vertical_prefix_.size(); ++i) vertical_prefix_[i] += vertical_prefix_[i - 1];
    for (size_t i = 1; i < horizontal_prefix_.size(); ++i) horizontal_prefix_[i] += horizontal_prefix_[i - 1];

    port_names_.reserve(generated::kPortNames.size());
    for (std::string_view name : generated::kPortNames) {
        const uint16_t id = static_cast<uint16_t>(port_names_.size());
        port_names_.emplace_back(name);
        port_id_.emplace(port_names_.back(), id);
    }
    arcs_from_.assign(port_names_.size(), {});
    arcs_to_.assign(port_names_.size(), {});
    nets_from_.assign(port_names_.size(), {});

    for (const generated::NetRecord& item : generated::kNets) {
        const uint16_t id = static_cast<uint16_t>(nets_.size());
        nets_.push_back({item.from, item.to, item.dx, item.dy});
        nets_from_[item.from].push_back(id);
    }
    internal_index_by_port_.assign(port_names_.size(), -1);
    for (const NetRule& net : nets_) {
        if (internal_index_by_port_[net.to_port] < 0) {
            internal_index_by_port_[net.to_port] = static_cast<int16_t>(internal_ports_.size());
            internal_ports_.push_back(net.to_port);
        }
    }
    macros_from_.assign(internal_ports_.size(), {});
    macros_to_.assign(internal_ports_.size(), {});

    for (const generated::ArcRecord& arc : generated::kArcs) {
        arcs_from_[arc.from].push_back({arc.to, arc.delay});
        arcs_to_[arc.to].push_back({arc.from, arc.delay});
        const int16_t from_internal = internal_index_by_port_[arc.from];
        if (from_internal < 0) continue;
        for (uint16_t net_id : nets_from_[arc.to]) {
            const int16_t to_internal = internal_index_by_port_[nets_[net_id].to_port];
            macros_from_[from_internal].push_back(
                {static_cast<uint16_t>(to_internal), net_id, arc.delay});
            macros_to_[to_internal].push_back(
                {static_cast<uint16_t>(from_internal), net_id, arc.delay});
        }
    }

    const size_t transition_count = site_x_.size() * nets_.size();
    forward_transitions_.resize(transition_count);
    reverse_transitions_.resize(transition_count);
    for (uint32_t site = 0; site < site_x_.size(); ++site) {
        for (uint16_t net = 0; net < nets_.size(); ++net) {
            const size_t index = static_cast<size_t>(site) * nets_.size() + net;
            forward_transitions_[index] = walk(site, nets_[net]);
            reverse_transitions_[index] = reverse_walk(site, nets_[net]);
        }
    }

    size_t macro_count = 0;
    for (const auto& edges : macros_from_) macro_count += edges.size();
    std::cerr << "architecture_embedded"
              << " sites=" << site_x_.size()
              << " ports=" << port_names_.size()
              << " internal_ports=" << internal_ports_.size()
              << " arcs=" << generated::kArcs.size()
              << " nets=" << nets_.size()
              << " macros=" << macro_count
              << " transition_mib=" << (2.0 * transition_count * sizeof(Transition) / 1048576.0)
              << " init_sec="
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << '\n';
}

}  // namespace srb

