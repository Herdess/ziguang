#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace srb {

inline constexpr uint32_t kInfinity = UINT32_MAX / 4;
inline constexpr uint32_t kInvalidSite = UINT32_MAX;

struct ArcEdge {
    uint16_t port = 0;
    uint16_t delay = 0;
};

struct NetRule {
    uint16_t from_port = 0;
    uint16_t to_port = 0;
    int16_t dx = 0;
    int16_t dy = 0;
};

struct MacroEdge {
    uint16_t internal = 0;
    uint16_t net = 0;
    uint16_t arc_delay = 0;
};

// Eight bytes per precomputed transition.  Block and Line delay are kept
// separate so exact and relative-Gap-estimate modes share the same table.
struct Transition {
    uint32_t site = kInvalidSite;
    uint16_t block_delay = 0;
    uint16_t line_delay = 0;
};

struct Pin {
    uint32_t site = kInvalidSite;
    uint16_t port = 0;
};

class Architecture {
public:
    Architecture();

    Pin parse_pin(std::string_view text) const;
    uint32_t internal_count() const { return static_cast<uint32_t>(internal_ports_.size()); }
    uint32_t site_count() const { return static_cast<uint32_t>(site_x_.size()); }
    uint32_t port_count() const { return static_cast<uint32_t>(port_names_.size()); }
    uint32_t net_count() const { return static_cast<uint32_t>(nets_.size()); }
    int width() const { return width_; }
    int height() const { return height_; }
    uint16_t site_x(uint32_t site) const { return site_x_[site]; }
    uint16_t site_y(uint32_t site) const { return site_y_[site]; }
    uint16_t internal_port(uint16_t internal) const { return internal_ports_[internal]; }
    const NetRule& net_rule(uint16_t net) const { return nets_[net]; }
    std::string format_pin(const Pin& pin) const;

    int16_t internal_index(uint16_t port) const { return internal_index_by_port_[port]; }
    int16_t internal_index_for_net_destination(uint16_t net) const;
    const std::vector<ArcEdge>& arcs_from(uint16_t port) const { return arcs_from_[port]; }
    const std::vector<ArcEdge>& arcs_to(uint16_t port) const { return arcs_to_[port]; }
    const std::vector<uint16_t>& nets_from(uint16_t port) const { return nets_from_[port]; }
    const std::vector<MacroEdge>& macros_from(uint16_t internal) const { return macros_from_[internal]; }
    const std::vector<MacroEdge>& macros_to(uint16_t internal) const { return macros_to_[internal]; }

    const Transition& forward_transition(uint32_t site, uint16_t net) const;
    const Transition& reverse_transition(uint32_t site, uint16_t net) const;
    uint32_t relative_line_delay(uint32_t from_site, uint32_t to_site) const;
    size_t static_memory_bytes() const;

private:
    struct GapBlock {
        int16_t lower = 0;
        int16_t upper = -1;
        int16_t left = 0;
        int16_t right = -1;
        bool vertical_crossable = false;
        uint16_t vertical_delay = 0;
        bool horizontal_crossable = false;
        uint16_t horizontal_delay = 0;
    };

    int width_ = 0;
    int height_ = 0;
    std::vector<std::string> port_names_;
    std::unordered_map<std::string, uint16_t> port_id_;
    std::vector<int32_t> physical_to_compact_;
    std::vector<uint16_t> site_x_;
    std::vector<uint16_t> site_y_;
    std::vector<int16_t> block_at_;
    std::vector<GapBlock> blocks_;
    std::vector<uint32_t> vertical_prefix_;
    std::vector<uint32_t> horizontal_prefix_;
    std::vector<std::vector<ArcEdge>> arcs_from_;
    std::vector<std::vector<ArcEdge>> arcs_to_;
    std::vector<NetRule> nets_;
    std::vector<std::vector<uint16_t>> nets_from_;
    std::vector<uint16_t> internal_ports_;
    std::vector<int16_t> internal_index_by_port_;
    std::vector<std::vector<MacroEdge>> macros_from_;
    std::vector<std::vector<MacroEdge>> macros_to_;
    std::vector<Transition> forward_transitions_;
    std::vector<Transition> reverse_transitions_;

    int physical_site(int x, int y) const { return y * width_ + x; }
    uint32_t line_delay(int x0, int y0, int x1, int y1) const;
    Transition walk(uint32_t compact_site, const NetRule& net) const;
    Transition reverse_walk(uint32_t destination, const NetRule& net) const;
    void build();
};

}  // namespace srb

