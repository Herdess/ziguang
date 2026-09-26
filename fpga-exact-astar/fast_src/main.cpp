#include "architecture.hpp"
#include "astar.hpp"
#include "csv_io.hpp"
#include "dijkstra.hpp"
#include "heuristic.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path input;
    fs::path output;
    fs::path path_output;
    uint64_t limit = 0;
    uint64_t progress = 0;
    srb::DelayMode mode = srb::DelayMode::Exact;
    srb::MarginConfig margin;
    std::string solver = "astar";
    std::string from;
    std::string to;
};

void usage(const char* program) {
    std::cerr
        << "Usage: " << program << " --input REQUEST.csv --output RESULT.csv [options]\n"
        << "   or: " << program << " --from PIN --to PIN [options]\n"
        << "Options:\n"
        << "  --solver astar|dijkstra|verify\n"
        << "                      exact A* (default), original bidirectional Dijkstra,\n"
        << "                      or run both and require identical delays\n"
        << "  --relative-gap-est  search without Gap Line weights, then add mandatory\n"
        << "                      Gap Line delay from the From/To relative position\n"
        << "  --margin N|auto     restrict search to the endpoint box expanded by N sites;\n"
        << "                      auto covers all legal source first-hop landings\n"
        << "  --limit N           process only the first N data rows (0 means all)\n"
        << "  --progress N        print progress every N rows (0 disables)\n";
}

uint64_t parse_u64(const std::string& text, const std::string& option) {
    size_t consumed = 0;
    unsigned long long value = 0;
    try {
        value = std::stoull(text, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error(option + " expects a non-negative integer");
    }
    if (consumed != text.size()) throw std::runtime_error(option + " expects a non-negative integer");
    return static_cast<uint64_t>(value);
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(name + " requires a value");
            return argv[++i];
        };
        if (arg == "--input" || arg == "-i") options.input = value(arg);
        else if (arg == "-in") options.input = value(arg);
        else if (arg == "--output" || arg == "-o") options.output = value(arg);
        else if (arg == "-out") options.output = value(arg);
        else if (arg == "--path-output" || arg == "--path-out") options.path_output = value(arg);
        else if (arg == "--arch") (void)value(arg);
        else if (arg == "--solver") options.solver = value(arg);
        else if (arg == "--from") options.from = value(arg);
        else if (arg == "--to") options.to = value(arg);
        else if (arg == "--limit") options.limit = parse_u64(value(arg), arg);
        else if (arg == "--progress") options.progress = parse_u64(value(arg), arg);
        else if (arg == "--margin") {
            const std::string text = value(arg);
            if (text == "auto") {
                options.margin = {srb::MarginMode::Auto, 0};
            } else {
                const uint64_t parsed = parse_u64(text, arg);
                if (parsed > std::numeric_limits<uint32_t>::max()) {
                    throw std::runtime_error("--margin is too large");
                }
                options.margin = {srb::MarginMode::Fixed, static_cast<uint32_t>(parsed)};
            }
        }
        else if (arg == "--relative-gap-est" || arg == "--est") {
            options.mode = srb::DelayMode::RelativeGapEstimate;
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }
    if (options.solver != "astar" && options.solver != "dijkstra" && options.solver != "verify") {
        throw std::runtime_error("--solver must be astar, dijkstra, or verify");
    }
    const bool single = !options.from.empty() || !options.to.empty();
    if (single && (options.from.empty() || options.to.empty())) {
        throw std::runtime_error("--from and --to must be used together");
    }
    if (!single && (options.input.empty() || options.output.empty())) {
        throw std::runtime_error("--input and --output are required for batch mode");
    }
    if (options.solver != "dijkstra" && options.mode != srb::DelayMode::Exact) {
        throw std::runtime_error("Exact A* supports exact delay mode only");
    }
    if (options.solver != "dijkstra" && options.margin.mode != srb::MarginMode::Disabled) {
        throw std::runtime_error("Exact A* does not use a search box; omit --margin");
    }
    return options;
}

const char* mode_name(srb::DelayMode mode) {
    return mode == srb::DelayMode::Exact ? "exact" : "relative_gap_estimate";
}

std::string format_path(const srb::Architecture& architecture,
                        const std::vector<srb::Pin>& path) {
    std::string text;
    for (size_t index = 0; index < path.size(); ++index) {
        if (index != 0) text += " | ";
        text += architecture.format_pin(path[index]);
    }
    return text;
}

std::string margin_name(const srb::MarginConfig& margin) {
    if (margin.mode == srb::MarginMode::Disabled) return "none";
    if (margin.mode == srb::MarginMode::Auto) return "auto";
    return std::to_string(margin.value);
}

}  // namespace

int main(int argc, char** argv) {
    const auto total_start = std::chrono::steady_clock::now();
    try {
        const Options options = parse_options(argc, argv);
        srb::Architecture architecture;
        std::unique_ptr<srb::BidirectionalDijkstra> dijkstra;
        std::unique_ptr<srb::DirectionalPotentialHeuristic> heuristic;
        std::unique_ptr<srb::ExactAStar> astar;
        if (options.solver == "dijkstra" || options.solver == "verify") {
            dijkstra = std::make_unique<srb::BidirectionalDijkstra>(architecture);
        }
        if (options.solver == "astar" || options.solver == "verify") {
            heuristic = std::make_unique<srb::DirectionalPotentialHeuristic>(architecture);
            astar = std::make_unique<srb::ExactAStar>(architecture, *heuristic);
        }

        srb::QueryStats stats;
        srb::QueryStats oracle_stats;
        auto solve = [&](const srb::Pin& source, const srb::Pin& target,
                         std::vector<srb::Pin>* path) {
            if (options.solver == "dijkstra") {
                return dijkstra->shortest(
                    source, target, options.mode, options.margin, stats, path);
            }
            const uint32_t answer = astar->shortest(source, target, stats, path);
            if (options.solver == "verify") {
                const uint32_t oracle = dijkstra->shortest(
                    source, target, srb::DelayMode::Exact, {}, oracle_stats, nullptr);
                if (answer != oracle) {
                    throw std::runtime_error(
                        "A*/Dijkstra mismatch: astar=" + std::to_string(answer) +
                        " dijkstra=" + std::to_string(oracle));
                }
            }
            return answer;
        };

        if (!options.from.empty()) {
            const srb::Pin source = architecture.parse_pin(options.from);
            const srb::Pin target = architecture.parse_pin(options.to);
            std::vector<srb::Pin> path;
            const uint32_t delay = solve(source, target, &path);
            if (delay == srb::kInfinity) std::cout << -1 << '\n';
            else std::cout << delay << '\n';
            if (delay != srb::kInfinity) {
                std::cout << format_path(architecture, path) << '\n';
            }
            return 0;
        }

        std::ifstream input(options.input);
        if (!input) throw std::runtime_error("cannot open input " + options.input.string());
        if (!options.output.parent_path().empty()) fs::create_directories(options.output.parent_path());
        std::ofstream output(options.output, std::ios::trunc);
        if (!output) throw std::runtime_error("cannot create output " + options.output.string());
        std::ofstream path_output;
        if (!options.path_output.empty()) {
            if (!options.path_output.parent_path().empty()) fs::create_directories(options.path_output.parent_path());
            path_output.open(options.path_output, std::ios::trunc);
            if (!path_output) throw std::runtime_error("cannot create path output " + options.path_output.string());
            path_output << "From,To,Min Delay,Path\n";
        }

        std::string header;
        if (!std::getline(input, header)) throw std::runtime_error("input CSV is empty");
        const auto header_pair = srb::parse_csv_pair(header);
        if (header_pair.first != "From" || header_pair.second != "To") {
            throw std::runtime_error("input header must begin with From,To");
        }
        output << "From,To,delay\n";

        uint64_t rows = 0;
        uint64_t unreachable = 0;
        std::string line;
        while ((options.limit == 0 || rows < options.limit) && std::getline(input, line)) {
            if (line.empty()) continue;
            const auto [from_text, to_text] = srb::parse_csv_pair(line);
            const srb::Pin source = architecture.parse_pin(from_text);
            const srb::Pin target = architecture.parse_pin(to_text);
            std::vector<srb::Pin> path;
            const uint32_t delay = solve(
                source, target, path_output ? &path : nullptr);
            output << from_text << ',' << to_text << ',';
            if (delay == srb::kInfinity) {
                output << -1;
                ++unreachable;
            } else {
                output << delay;
            }
            output << '\n';
            if (path_output) {
                path_output << from_text << ',' << to_text << ',';
                if (delay == srb::kInfinity) path_output << -1;
                else path_output << delay;
                path_output << ',';
                path_output << format_path(architecture, path);
                path_output << '\n';
            }
            ++rows;
            if (options.progress != 0 && rows % options.progress == 0) {
                const double elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - total_start).count();
                std::cerr << "progress rows=" << rows << " elapsed_sec=" << elapsed
                          << " avg_us=" << (elapsed * 1e6 / rows) << '\n';
            }
        }
        output.close();
        if (!output) throw std::runtime_error("failed while writing output " + options.output.string());
        if (path_output) {
            path_output.close();
            if (!path_output) throw std::runtime_error("failed while writing path output " + options.path_output.string());
        }

        const double runtime = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - total_start).count();
        size_t estimated_bytes = architecture.static_memory_bytes();
        if (dijkstra) estimated_bytes += dijkstra->workspace_bytes();
        if (heuristic) estimated_bytes += heuristic->memory_bytes();
        if (astar) estimated_bytes += astar->workspace_bytes();
        std::cerr << "completed"
                  << " solver=" << options.solver
                  << " mode=" << mode_name(options.mode)
                  << " margin=" << margin_name(options.margin)
                  << " rows=" << rows
                  << " unreachable=" << unreachable
                  << " settled_forward=" << stats.settled_forward
                  << " settled_backward=" << stats.settled_backward
                  << " relaxed=" << stats.relaxed
                  << " heuristic_evaluations=" << stats.heuristic_evaluations
                  << " bounded_queries=" << stats.bounded_queries
                  << " effective_margin_avg="
                  << (stats.bounded_queries
                          ? static_cast<double>(stats.effective_margin_sum) / stats.bounded_queries
                          : 0.0)
                  << " effective_margin_max=" << stats.effective_margin_max
                  << " runtime_sec=" << runtime
                  << " avg_us=" << (rows ? runtime * 1e6 / rows : 0.0)
                  << " estimated_core_mib=" << (estimated_bytes / 1048576.0)
                  << " output=" << options.output.string() << '\n';
        if (options.solver == "verify") {
            std::cerr << "oracle_stats"
                      << " settled_forward=" << oracle_stats.settled_forward
                      << " settled_backward=" << oracle_stats.settled_backward
                      << " relaxed=" << oracle_stats.relaxed << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        usage(argv[0]);
        return 1;
    }
}

