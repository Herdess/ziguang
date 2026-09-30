#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace solver {

inline constexpr uint32_t kInvalidPort = UINT32_MAX;

struct Pin {
    int x = 0;
    int y = 0;
    uint16_t port = 0;
};

struct NetRule {
    uint16_t from = 0;
    uint16_t to = 0;
    int16_t dx = 0;
    int16_t dy = 0;
};

struct ArcEdge {
    uint16_t to = 0;
    uint16_t delay = 0;
};

struct WalkResult {
    bool valid = false;
    int x = 0;
    int y = 0;
    uint32_t block_delay = 0;
    uint32_t line_delay = 0;
};

struct NormalizedSource {
    bool matched = false;
    int x = 0;
    int y = 0;
    uint32_t initial_gap_delay = 0;
    bool used_forced_net = false;
};

struct NormalizedTarget {
    bool matched = false;
    int x = 0;
    int y = 0;
    uint16_t port = 0;
    bool used_reverse_net = false;
};

struct SourceCandidate {
    int x = 0;
    int y = 0;
    uint16_t port = 0;
    uint32_t initial_delay = 0;
    uint32_t gap_block_delay = 0;
    uint32_t gap_line_delay = 0;
};

class Architecture {
public:
    Architecture();

    bool parse_pin(std::string_view text, Pin& result) const;
    std::string_view port_name(uint16_t port) const;
    uint32_t port_count() const { return static_cast<uint32_t>(port_names_.size()); }
    NormalizedSource normalize_source(const Pin& source, uint16_t golden_source_port) const;
    NormalizedTarget normalize_target_free(const Pin& target) const;
    std::vector<SourceCandidate> source_candidates(const Pin& source) const;
    int32_t direct_arc_delay(uint16_t from, uint16_t to) const;
    uint32_t line_delay(int x0, int y0, int x1, int y1) const;

private:
    struct GapBlock {
        int lower = 0;
        int upper = -1;
        int left = 0;
        int right = -1;
        bool vertical_crossable = false;
        uint16_t vertical_delay = 0;
        bool horizontal_crossable = false;
        uint16_t horizontal_delay = 0;
    };

    struct GapLine {
        uint8_t direction = 0;
        int site = 0;
        uint16_t delay = 0;
    };

    int width_ = 0;
    int height_ = 0;
    std::vector<std::string> port_names_;
    std::unordered_map<std::string, uint16_t> port_id_;
    std::vector<NetRule> nets_;
    std::vector<std::vector<ArcEdge>> arcs_from_;
    std::vector<int16_t> net_by_from_;
    std::vector<int16_t> net_by_to_;
    std::vector<uint8_t> is_output_port_;
    std::vector<GapBlock> blocks_;
    std::vector<GapLine> lines_;

    int block_index(int x, int y) const;
    bool valid_site(int x, int y) const;
    WalkResult walk(int x, int y, const NetRule& net) const;
};

}  // namespace solver
