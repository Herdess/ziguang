#include "grid_model.hpp"

#include "architecture.hpp"
#include "generated_arch_data.hpp"
#include "residual_corrections.hpp"

#include <algorithm>
#include <chrono>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#ifndef GRID_EXPECTED_SOURCES
#define GRID_EXPECTED_SOURCES 160
#endif

namespace fs = std::filesystem;
namespace gen = srb::generated;

namespace {

struct Options {
    fs::path input;
    fs::path model;
    fs::path output;
    bool line_estimate = true;
    bool always_fused_logic = true;
    uint64_t limit = std::numeric_limits<uint64_t>::max();
    uint64_t progress = 0;
};

void usage(const char* program) {
    std::cerr
        << "Usage: " << program << " -in FILE -out FILE [options]\n"
        << "\nOptions:\n"
        << "  --limit N              process at most N input rows\n"
        << "  --progress N           print progress every N rows\n"
        << "  --no-line-est          disable mandatory gap-line estimate\n"
        << "  --model FILE           override the model beside the executable\n"
        << "  --no-fused-logic       enumerate Logic-source candidates\n"
        << "  -h, --help             show this help\n";
}

uint64_t parse_u64(const std::string& text) {
    size_t used = 0;
    const uint64_t value = std::stoull(text, &used);
    if (used != text.size()) throw std::runtime_error("invalid integer option");
    return value;
}

Options options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            std::exit(0);
        }
        auto value = [&]() -> std::string {
            if (++i >= argc) throw std::runtime_error(arg + " requires a value");
            return argv[i];
        };
        if (arg == "-in" || arg == "--input") result.input = value();
        else if (arg == "--model") result.model = value();
        else if (arg == "-out" || arg == "--output") result.output = value();
        else if (arg == "--limit") result.limit = parse_u64(value());
        else if (arg == "--progress") result.progress = parse_u64(value());
        else if (arg == "--no-line-est") result.line_estimate = false;
        else if (arg == "--always-fused-logic") result.always_fused_logic = true;
        else if (arg == "--no-fused-logic") result.always_fused_logic = false;
        else throw std::runtime_error("unknown option: " + arg);
    }
    if (result.input.empty() || result.output.empty()) {
        throw std::runtime_error("required: -in FILE -out FILE");
    }
    if (result.model.empty()) {
        result.model = fs::absolute(fs::path(argv[0])).parent_path() / "grid208_4x8_u16.bin";
    }
    return result;
}

bool split_csv(std::string& line, std::string_view& from, std::string_view& to,
               std::string_view& delay) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t a = line.find(',');
    if (a == std::string::npos) return false;
    const size_t b = line.find(',', a + 1);
    from = std::string_view(line.data(), a);
    if (b == std::string::npos) {
        to = std::string_view(line.data() + a + 1, line.size() - a - 1);
        delay = {};
    } else {
        to = std::string_view(line.data() + a + 1, b - a - 1);
        delay = std::string_view(line.data() + b + 1, line.size() - b - 1);
    }
    return true;
}

uint32_t parse_delay(std::string_view text) {
    uint32_t value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') throw std::runtime_error("invalid reference delay");
        value = value * 10U + static_cast<uint32_t>(ch - '0');
    }
    return value;
}

uint64_t peak_rss_bytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    return ::GetProcessMemoryInfo(::GetCurrentProcess(), &counters, sizeof(counters))
        ? static_cast<uint64_t>(counters.PeakWorkingSetSize) : 0;
#else
    rusage usage{};
    return getrusage(RUSAGE_SELF, &usage) == 0 ? static_cast<uint64_t>(usage.ru_maxrss) * 1024U : 0;
#endif
}

struct PortKinds {
    std::array<uint8_t, gen::kPortNames.size()> internal_input{};
    std::array<uint16_t, gen::kPortNames.size()> free_candidate_count{};

    PortKinds() {
        std::array<int16_t, gen::kPortNames.size()> net_by_from{};
        net_by_from.fill(-1);
        for (size_t index = 0; index < gen::kNets.size(); ++index) {
            const gen::NetRecord& net = gen::kNets[index];
            net_by_from[net.from] = static_cast<int16_t>(index);
            internal_input[net.to] = 1;
        }
        for (const gen::ArcRecord& arc : gen::kArcs) {
            if (!internal_input[arc.from] && net_by_from[arc.to] >= 0) {
                ++free_candidate_count[arc.from];
            }
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto begin = std::chrono::steady_clock::now();
        const Options opt = options(argc, argv);
        const solver::Architecture architecture;
        const grid16::Model model(opt.model);
        const PortKinds port_kinds;
        const solver::ResidualCorrections residual_corrections;
        if (model.source_count() != GRID_EXPECTED_SOURCES) {
            throw std::runtime_error("unexpected source count in grid model");
        }
        std::ifstream input(opt.input);
        if (!input) throw std::runtime_error("cannot open input");
        std::string line;
        if (!std::getline(input, line)) throw std::runtime_error("empty input");
        const bool validation_mode = std::count(line.begin(), line.end(), ',') >= 2;
        if (!opt.output.parent_path().empty()) fs::create_directories(opt.output.parent_path());
        std::ofstream output(opt.output, std::ios::trunc);
        if (!output) throw std::runtime_error("cannot create output");
        if (validation_mode) {
            output << "From,To,delay,ans,ans_without_gap_block,gap_block_added,gap_block_delay,"
                      "source_candidates,direct_arc,fused_logic\n";
        } else {
            output << "From,To,delay\n";
        }

        uint64_t rows = 0, matched = 0, malformed = 0, source_miss = 0;
        uint64_t target_miss = 0, unreachable = 0, block_added = 0;
        uint64_t candidate_lookups = 0, direct_arc = 0, fused_logic = 0, residual_adjusted = 0;
        while (rows < opt.limit && std::getline(input, line)) {
            ++rows;
            std::string_view from_text, to_text, delay_text;
            solver::Pin from, to;
            if (!split_csv(line, from_text, to_text, delay_text) ||
                !architecture.parse_pin(from_text, from) || !architecture.parse_pin(to_text, to)) {
                ++malformed;
                if (!validation_mode) {
                    throw std::runtime_error("malformed request at row " + std::to_string(rows));
                }
                continue;
            }
            const uint32_t reference = validation_mode ? parse_delay(delay_text) : 0U;
            uint32_t best = std::numeric_limits<uint32_t>::max();
            uint32_t best_block = 0;
            bool best_direct = false;
            if (from.x == to.x && from.y == to.y && from.port == to.port) best = 0;
            if (from.x == to.x && from.y == to.y) {
                const int32_t delay = architecture.direct_arc_delay(from.port, to.port);
                if (delay >= 0 && static_cast<uint32_t>(delay) < best) {
                    best = static_cast<uint32_t>(delay);
                    best_direct = true;
                }
            }
            const solver::NormalizedTarget target = architecture.normalize_target_free(to);
            if (!target.matched || !model.has_target(target.port)) {
                ++target_miss;
                if (!validation_mode) {
                    throw std::runtime_error("unsupported target at row " + std::to_string(rows));
                }
                continue;
            }
            bool force_fused_logic = false;
            if constexpr (GRID_EXPECTED_SOURCES > 160) {
                force_fused_logic = opt.always_fused_logic && !port_kinds.internal_input[from.port] &&
                    model.has_source(from.port) && port_kinds.free_candidate_count[from.port] != 0;
            }
            const std::vector<solver::SourceCandidate> candidates = force_fused_logic
                ? std::vector<solver::SourceCandidate>{}
                : architecture.source_candidates(from);
            if (candidates.empty() && !force_fused_logic &&
                best == std::numeric_limits<uint32_t>::max()) {
                ++source_miss;
                if (!validation_mode) {
                    throw std::runtime_error("unsupported source at row " + std::to_string(rows));
                }
                continue;
            }
            bool use_fused_logic = false;
            if (force_fused_logic) {
                use_fused_logic = true;
            } else if constexpr (GRID_EXPECTED_SOURCES > 160) {
                if (!port_kinds.internal_input[from.port] && model.has_source(from.port) &&
                    port_kinds.free_candidate_count[from.port] != 0 &&
                    candidates.size() == port_kinds.free_candidate_count[from.port]) {
                    use_fused_logic = true;
                    for (const solver::SourceCandidate& candidate : candidates) {
                        if (candidate.gap_block_delay != 0) {
                            use_fused_logic = false;
                            break;
                        }
                    }
                }
            }
            if (use_fused_logic) {
                const uint16_t base = model.lookup(from.port, target.port,
                    target.x - from.x, target.y - from.y);
                ++candidate_lookups;
                if (base != grid16::kUnreachable) {
                    uint32_t answer = base;
                    if (opt.line_estimate) answer += architecture.line_delay(from.x, from.y, to.x, to.y);
                    if (answer < best) {
                        best = answer;
                        best_block = 0;
                        best_direct = false;
                    }
                    ++fused_logic;
                }
            } else {
                for (const solver::SourceCandidate& candidate : candidates) {
                    const uint16_t base = model.lookup(candidate.port, target.port,
                        target.x - candidate.x, target.y - candidate.y);
                    ++candidate_lookups;
                    if (base == grid16::kUnreachable) continue;
                    uint32_t answer = static_cast<uint32_t>(base) + candidate.initial_delay;
                    if (opt.line_estimate) {
                        answer += architecture.line_delay(candidate.x, candidate.y, to.x, to.y);
                    }
                    if (answer < best || (answer == best && candidate.gap_block_delay < best_block)) {
                        best = answer;
                        best_block = candidate.gap_block_delay;
                        best_direct = false;
                    }
                }
            }
            if (best == std::numeric_limits<uint32_t>::max()) {
                ++unreachable;
                if (!validation_mode) {
                    throw std::runtime_error("unreachable request at row " + std::to_string(rows));
                }
                continue;
            }
            const int residual_adjustment = residual_corrections.adjustment(
                from.port, to.port, to.x - from.x, to.y - from.y);
            // The sparse rules are translation invariant. Do not apply them to a route whose
            // selected source candidate crossed an absolute-position gap block.
            if (residual_adjustment != 0 && best_block == 0) {
                const int64_t adjusted = static_cast<int64_t>(best) + residual_adjustment;
                best = static_cast<uint32_t>(std::max<int64_t>(0, adjusted));
                ++residual_adjusted;
            }
            if (validation_mode) {
                output << from_text << ',' << to_text << ',' << reference << ',' << best << ','
                       << (best - best_block) << ',' << (best_block ? 1 : 0) << ',' << best_block << ','
                       << (force_fused_logic ? port_kinds.free_candidate_count[from.port] : candidates.size())
                       << ',' << (best_direct ? 1 : 0) << ','
                       << (use_fused_logic ? 1 : 0) << '\n';
            } else {
                output << from_text << ',' << to_text << ',' << best << '\n';
            }
            ++matched;
            block_added += best_block != 0;
            direct_arc += best_direct;
            if (opt.progress && rows % opt.progress == 0) {
                std::cerr << "progress rows=" << rows << " matched=" << matched << '\n';
            }
        }
        output.close();
        const double runtime = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - begin).count();
        std::cerr << std::fixed << std::setprecision(6)
                  << "completed rows=" << rows << " matched=" << matched
                  << " malformed=" << malformed << " source_miss=" << source_miss
                  << " target_miss=" << target_miss << " unreachable=" << unreachable
                  << " gap_block_added=" << block_added << " direct_arc=" << direct_arc
                  << " fused_logic=" << fused_logic << " candidate_lookups=" << candidate_lookups
                  << " residual_adjusted=" << residual_adjusted
                  << " model_gib=" << static_cast<double>(model.mapped_bytes()) / 1073741824.0
                  << " peak_rss_mib=" << static_cast<double>(peak_rss_bytes()) / 1048576.0
                  << " runtime_sec=" << runtime
                  << " avg_ns=" << (rows ? runtime * 1e9 / rows : 0.0) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
