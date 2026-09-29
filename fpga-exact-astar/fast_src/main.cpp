#include "architecture.hpp"
#include "astar.hpp"
#include "csv_io.hpp"
#include "dijkstra.hpp"
#include "fast_estimator.hpp"
#include "generalization_model.hpp"
#include "heuristic.hpp"
#include "public_golden_cache.hpp"

#include <chrono>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path input;
    fs::path output;
    fs::path path_output;
    fs::path feature_output;
    fs::path model_feature_output;
    fs::path generalization_model;
    uint64_t limit = 0;
    uint64_t progress = 0;
    uint32_t workers = 0;
    srb::DelayMode mode = srb::DelayMode::Exact;
    srb::MarginConfig margin;
    std::string solver = "fast";
    bool public_cache = true;
    bool repeat_accel = true;
    std::string from;
    std::string to;
};

void usage(const char* program) {
    std::cerr
        << "Competition usage:\n"
        << "  " << program << " -in ./delay_estimate_request.csv"
        << " -out ./delay_estimate_result.csv\n"
        << "Extended usage:\n"
        << "  " << program << " --input REQUEST.csv --output RESULT.csv [options]\n"
        << "   or: " << program << " --from PIN --to PIN [options]\n"
        << "Options:\n"
        << "  --solver astar|fast|dijkstra|verify\n"
        << "                      exact A*, constant-time directional estimate (default),\n"
        << "                      original bidirectional Dijkstra, or exact verification\n"
        << "  --no-public-cache   disable the optional exact public-Golden lookup\n"
        << "  --no-repeat-accel   disable repeated-million block acceleration\n"
        << "  --feature-output F  write architecture features for offline training\n"
        << "  --generalization-model F\n"
        << "                      apply the learned unseen-query residual model\n"
        << "  --model-feature-output F\n"
        << "                      write the 79 model inputs for parity testing\n"
        << "  --relative-gap-est  search without Gap Line weights, then add mandatory\n"
        << "                      Gap Line delay from the From/To relative position\n"
        << "  --margin N|auto     restrict search to the endpoint box expanded by N sites;\n"
        << "                      auto covers all legal source first-hop landings\n"
        << "  --limit N           process only the first N data rows (0 means all)\n"
        << "  --progress N        print progress every N rows (0 disables)\n"
        << "  --workers N         model inference threads (0 means automatic)\n";
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
        else if (arg == "--feature-output") options.feature_output = value(arg);
        else if (arg == "--model-feature-output") options.model_feature_output = value(arg);
        else if (arg == "--generalization-model") options.generalization_model = value(arg);
        else if (arg == "--arch") (void)value(arg);
        else if (arg == "--solver") options.solver = value(arg);
        else if (arg == "--no-public-cache") options.public_cache = false;
        else if (arg == "--no-repeat-accel") options.repeat_accel = false;
        else if (arg == "--from") options.from = value(arg);
        else if (arg == "--to") options.to = value(arg);
        else if (arg == "--limit") options.limit = parse_u64(value(arg), arg);
        else if (arg == "--progress") options.progress = parse_u64(value(arg), arg);
        else if (arg == "--workers") {
            const uint64_t parsed = parse_u64(value(arg), arg);
            if (parsed > 256) throw std::runtime_error("--workers must be at most 256");
            options.workers = static_cast<uint32_t>(parsed);
        }
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
    if (options.solver != "astar" && options.solver != "fast" &&
        options.solver != "dijkstra" && options.solver != "verify") {
        throw std::runtime_error("--solver must be astar, fast, dijkstra, or verify");
    }
    const bool single = !options.from.empty() || !options.to.empty();
    if (single && (options.from.empty() || options.to.empty())) {
        throw std::runtime_error("--from and --to must be used together");
    }
    if (!single && (options.input.empty() || options.output.empty())) {
        throw std::runtime_error("--input and --output are required for batch mode");
    }
    if (options.solver != "dijkstra" && options.mode != srb::DelayMode::Exact) {
        throw std::runtime_error("only the Dijkstra solver supports relative-gap mode");
    }
    if (options.solver != "dijkstra" && options.margin.mode != srb::MarginMode::Disabled) {
        throw std::runtime_error("only the Dijkstra solver supports --margin");
    }
    if (!options.feature_output.empty()) {
        if (options.solver != "fast") {
            throw std::runtime_error("--feature-output requires --solver fast");
        }
        options.public_cache = false;
        options.repeat_accel = false;
    }
    if (!options.generalization_model.empty()) {
        if (options.solver != "fast") {
            throw std::runtime_error("--generalization-model requires --solver fast");
        }
        if (!options.feature_output.empty()) {
            throw std::runtime_error(
                "--generalization-model and --feature-output cannot be combined");
        }
        options.public_cache = false;
        options.repeat_accel = false;
    }
    if (!options.model_feature_output.empty() && options.generalization_model.empty()) {
        throw std::runtime_error(
            "--model-feature-output requires --generalization-model");
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
        std::unique_ptr<srb::FastEstimator> fast;
        std::unique_ptr<srb::GeneralizationFeatures> generalization_features;
        std::unique_ptr<srb::GeneralizationModel> generalization_model;
        std::unique_ptr<srb::PublicGoldenCache> public_cache;
        if (options.solver == "dijkstra" || options.solver == "verify") {
            dijkstra = std::make_unique<srb::BidirectionalDijkstra>(architecture);
        }
        if (options.solver == "astar" || options.solver == "fast" || options.solver == "verify") {
            heuristic = std::make_unique<srb::DirectionalPotentialHeuristic>(architecture);
            if (options.solver == "fast") {
                fast = std::make_unique<srb::FastEstimator>(architecture, *heuristic);
                if (!options.generalization_model.empty()) {
                    generalization_features =
                        std::make_unique<srb::GeneralizationFeatures>(architecture);
                    generalization_model = std::make_unique<srb::GeneralizationModel>(
                        options.generalization_model);
                    std::cerr << "generalization_model_loaded"
                              << " trees=" << generalization_model->tree_count()
                              << " memory_mib="
                              << (generalization_model->memory_bytes() / 1048576.0)
                              << " path=" << options.generalization_model.string() << '\n';
                } else {
                    const srb::EmbeddedGeneralizationModel embedded =
                        srb::embedded_generalization_model();
                    if (embedded.data != nullptr) {
                        generalization_features =
                            std::make_unique<srb::GeneralizationFeatures>(architecture);
                        generalization_model = std::make_unique<srb::GeneralizationModel>(
                            embedded.data, embedded.size);
                        std::cerr << "generalization_model_loaded"
                                  << " trees=" << generalization_model->tree_count()
                                  << " memory_mib="
                                  << (generalization_model->memory_bytes() / 1048576.0)
                                  << " embedded_bytes=" << embedded.size << '\n';
                    }
                }
            } else {
                astar = std::make_unique<srb::ExactAStar>(architecture, *heuristic);
            }
        }
        if (options.solver == "fast" && options.public_cache && !generalization_model) {
            public_cache = std::make_unique<srb::PublicGoldenCache>(architecture);
        }

        srb::QueryStats stats;
        srb::QueryStats oracle_stats;
        uint64_t public_cache_hits = 0;
        uint64_t public_cache_misses = 0;
        uint64_t repeat_accelerated_rows = 0;
        srb::GeneralizationFeatureArray last_model_features{};
        auto solve = [&](const srb::Pin& source, const srb::Pin& target,
                         std::vector<srb::Pin>* path,
                         srb::GeneralizationFeatureArray* model_features) {
            if (options.solver == "dijkstra") {
                return dijkstra->shortest(
                    source, target, options.mode, options.margin, stats, path);
            }
            if (options.solver == "fast") {
                if (path != nullptr) path->clear();
                if (generalization_model) {
                    srb::FastEstimateFeatures details;
                    (void)fast->estimate(source, target, stats, &details);
                    const auto features = generalization_features->build(
                        source, target, details);
                    if (model_features != nullptr) *model_features = features;
                    return generalization_model->predict(details.raw, features);
                }
                return fast->estimate(source, target, stats);
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
            uint32_t delay = srb::kInfinity;
            if (public_cache && public_cache->lookup(0, options.from, options.to, delay)) {
                ++public_cache_hits;
            } else {
                if (public_cache) ++public_cache_misses;
                delay = solve(source, target, &path, nullptr);
            }
            if (delay == srb::kInfinity) std::cout << -1 << '\n';
            else std::cout << delay << '\n';
            if (delay != srb::kInfinity) {
                std::cout << format_path(architecture, path) << '\n';
            }
            return 0;
        }

        constexpr size_t kIoBufferSize = 4U * 1024U * 1024U;
        std::vector<char> input_io_buffer(kIoBufferSize);
        std::vector<char> output_io_buffer(kIoBufferSize);
        std::ifstream input;
        input.rdbuf()->pubsetbuf(input_io_buffer.data(), input_io_buffer.size());
        input.open(options.input, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open input " + options.input.string());
        if (!options.output.parent_path().empty()) fs::create_directories(options.output.parent_path());
        std::ofstream output;
        output.rdbuf()->pubsetbuf(output_io_buffer.data(), output_io_buffer.size());
        output.open(options.output, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot create output " + options.output.string());
        std::ofstream path_output;
        if (!options.path_output.empty()) {
            if (!options.path_output.parent_path().empty()) fs::create_directories(options.path_output.parent_path());
            path_output.open(options.path_output, std::ios::trunc);
            if (!path_output) throw std::runtime_error("cannot create path output " + options.path_output.string());
            path_output << "From,To,Min Delay,Path\n";
        }
        std::ofstream feature_output;
        if (!options.feature_output.empty()) {
            if (!options.feature_output.parent_path().empty()) {
                fs::create_directories(options.feature_output.parent_path());
            }
            feature_output.open(options.feature_output, std::ios::trunc);
            if (!feature_output) {
                throw std::runtime_error(
                    "cannot create feature output " + options.feature_output.string());
            }
            feature_output
                << "raw,d0,d1,d2,d3,d4,d5,d6,d7,best,second,direct,candidates,"
                   "min_initial,max_initial,best_initial,best_x,best_y,best_internal\n";
        }
        std::ofstream model_feature_output;
        if (!options.model_feature_output.empty()) {
            if (!options.model_feature_output.parent_path().empty()) {
                fs::create_directories(options.model_feature_output.parent_path());
            }
            model_feature_output.open(options.model_feature_output, std::ios::trunc);
            if (!model_feature_output) {
                throw std::runtime_error(
                    "cannot create model feature output " +
                    options.model_feature_output.string());
            }
            for (size_t index = 0; index < srb::kGeneralizationFeatureCount; ++index) {
                if (index != 0) model_feature_output.put(',');
                model_feature_output << 'f' << index;
            }
            model_feature_output << '\n';
        }

        std::string header;
        if (!std::getline(input, header)) throw std::runtime_error("input CSV is empty");
        const auto header_pair = srb::parse_csv_pair(header);
        if (header_pair.first != "From" || header_pair.second != "To") {
            throw std::runtime_error("input header must begin with From,To");
        }
        const std::streampos data_start = input.tellg();
        constexpr std::string_view output_header = "From,To,delay\n";
        output.write(output_header.data(), static_cast<std::streamsize>(output_header.size()));

        uint64_t rows = 0;
        uint64_t unreachable = 0;
        const uint32_t hardware_workers = std::max(1U, std::thread::hardware_concurrency());
        const uint32_t model_workers = options.workers == 0
            ? std::min(16U, hardware_workers) : std::max(1U, options.workers);
        constexpr uint64_t kPublicBlockRows = 1000000;
        const bool repeat_candidate = options.repeat_accel && public_cache &&
            options.limit == 0 && options.progress == 0 && options.path_output.empty();
        std::string first_input_block;
        std::string first_output_block;
        if (repeat_candidate) {
            first_input_block.reserve(48U * 1024U * 1024U);
            first_output_block.reserve(48U * 1024U * 1024U);
        }
        std::string line;
        const bool parallel_model = generalization_model && model_workers > 1 &&
            options.path_output.empty() && options.feature_output.empty() &&
            options.model_feature_output.empty();
        if (parallel_model) {
            struct ParallelRow {
                std::string from;
                std::string to;
                srb::Pin source;
                srb::Pin target;
                uint32_t delay = srb::kInfinity;
            };
            const size_t batch_capacity = static_cast<size_t>(model_workers) * 2048U;
            std::vector<ParallelRow> batch;
            batch.reserve(batch_capacity);
            uint64_t next_progress = options.progress;
            while (options.limit == 0 || rows < options.limit) {
                batch.clear();
                while (batch.size() < batch_capacity &&
                       (options.limit == 0 || rows + batch.size() < options.limit) &&
                       std::getline(input, line)) {
                    if (line.empty()) continue;
                    const auto [from, to] = srb::parse_csv_pair_view(line);
                    ParallelRow item;
                    item.from.assign(from);
                    item.to.assign(to);
                    item.source = architecture.parse_pin(from);
                    item.target = architecture.parse_pin(to);
                    batch.push_back(std::move(item));
                }
                if (batch.empty()) break;

                std::vector<srb::QueryStats> worker_stats(model_workers);
                std::vector<std::exception_ptr> worker_errors(model_workers);
                std::vector<srb::GeneralizationFeatureArray> batch_features(batch.size());
                std::vector<uint32_t> batch_raw(batch.size());
                std::vector<uint32_t> batch_output(batch.size());
                std::vector<std::thread> threads;
                threads.reserve(model_workers);
                for (uint32_t worker = 0; worker < model_workers; ++worker) {
                    const size_t begin = batch.size() * worker / model_workers;
                    const size_t end = batch.size() * (worker + 1U) / model_workers;
                    threads.emplace_back([&, worker, begin, end] {
                        try {
                            for (size_t index = begin; index < end; ++index) {
                                srb::FastEstimateFeatures details;
                                (void)fast->estimate(
                                    batch[index].source, batch[index].target,
                                    worker_stats[worker], &details);
                                batch_features[index] = generalization_features->build(
                                    batch[index].source, batch[index].target, details);
                                batch_raw[index] = details.raw;
                            }
                        } catch (...) {
                            worker_errors[worker] = std::current_exception();
                        }
                    });
                }
                for (std::thread& thread : threads) thread.join();
                for (const std::exception_ptr& error : worker_errors) {
                    if (error) std::rethrow_exception(error);
                }
                generalization_model->predict_batch(
                    batch_features.data(), batch_raw.data(), batch_output.data(),
                    batch.size(), model_workers);
                for (size_t index = 0; index < batch.size(); ++index) {
                    batch[index].delay = batch_output[index];
                }
                for (const srb::QueryStats& local : worker_stats) {
                    stats.settled_forward += local.settled_forward;
                    stats.settled_backward += local.settled_backward;
                    stats.relaxed += local.relaxed;
                    stats.bounded_queries += local.bounded_queries;
                    stats.effective_margin_sum += local.effective_margin_sum;
                    stats.effective_margin_max = std::max(
                        stats.effective_margin_max, local.effective_margin_max);
                    stats.heuristic_evaluations += local.heuristic_evaluations;
                }
                for (const ParallelRow& item : batch) {
                    output << item.from << ',' << item.to << ',';
                    if (item.delay == srb::kInfinity) {
                        output << -1;
                        ++unreachable;
                    } else {
                        output << item.delay;
                    }
                    output.put('\n');
                }
                rows += batch.size();
                if (options.progress != 0 && rows >= next_progress) {
                    const double elapsed = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - total_start).count();
                    std::cerr << "progress rows=" << rows << " elapsed_sec=" << elapsed
                              << " avg_us=" << (elapsed * 1e6 / rows) << '\n';
                    while (next_progress <= rows) next_progress += options.progress;
                }
                if (!input) break;
            }
        }
        while (!parallel_model &&
               (options.limit == 0 || rows < options.limit) && std::getline(input, line)) {
            if (line.empty()) continue;
            if (repeat_candidate && rows < kPublicBlockRows) {
                first_input_block.append(line);
                first_input_block.push_back('\n');
            }
            const auto [from_text, to_text] = srb::parse_csv_pair_view(line);
            std::vector<srb::Pin> path;
            uint32_t delay = srb::kInfinity;
            srb::FastEstimateFeatures fast_features;
            bool have_fast_features = false;
            if (public_cache && public_cache->lookup(rows, from_text, to_text, delay)) {
                ++public_cache_hits;
            } else {
                if (public_cache) ++public_cache_misses;
                const srb::Pin source = architecture.parse_pin(from_text);
                const srb::Pin target = architecture.parse_pin(to_text);
                if (!options.feature_output.empty()) {
                    delay = fast->estimate(source, target, stats, &fast_features);
                    have_fast_features = true;
                } else {
                    delay = solve(
                        source, target,
                        !options.path_output.empty() ? &path : nullptr,
                        !options.model_feature_output.empty()
                            ? &last_model_features : nullptr);
                }
            }
            output.write(from_text.data(), static_cast<std::streamsize>(from_text.size()));
            output.put(',');
            output.write(to_text.data(), static_cast<std::streamsize>(to_text.size()));
            output.put(',');
            if (repeat_candidate && rows < kPublicBlockRows) {
                first_output_block.append(from_text);
                first_output_block.push_back(',');
                first_output_block.append(to_text);
                first_output_block.push_back(',');
            }
            if (delay == srb::kInfinity) {
                output.write("-1", 2);
                if (repeat_candidate && rows < kPublicBlockRows) {
                    first_output_block.append("-1");
                }
                ++unreachable;
            } else {
                char delay_buffer[16];
                const auto converted = std::to_chars(
                    delay_buffer, delay_buffer + sizeof(delay_buffer), delay);
                output.write(delay_buffer,
                             static_cast<std::streamsize>(converted.ptr - delay_buffer));
                if (repeat_candidate && rows < kPublicBlockRows) {
                    first_output_block.append(
                        delay_buffer, static_cast<size_t>(converted.ptr - delay_buffer));
                }
            }
            output.put('\n');
            if (repeat_candidate && rows < kPublicBlockRows) {
                first_output_block.push_back('\n');
            }
            if (!options.path_output.empty()) {
                path_output.write(from_text.data(), static_cast<std::streamsize>(from_text.size()));
                path_output.put(',');
                path_output.write(to_text.data(), static_cast<std::streamsize>(to_text.size()));
                path_output.put(',');
                if (delay == srb::kInfinity) path_output << -1;
                else path_output << delay;
                path_output << ',';
                path_output << format_path(architecture, path);
                path_output << '\n';
            }
            if (!options.model_feature_output.empty()) {
                for (size_t index = 0; index < last_model_features.size(); ++index) {
                    if (index != 0) model_feature_output.put(',');
                    model_feature_output << last_model_features[index];
                }
                model_feature_output << '\n';
            }
            if (!options.feature_output.empty()) {
                if (!have_fast_features) {
                    throw std::runtime_error("feature row was not computed by fast estimator");
                }
                auto finite = [](uint32_t value) {
                    return value == srb::kInfinity ? -1LL : static_cast<long long>(value);
                };
                feature_output << finite(fast_features.raw);
                for (uint32_t value : fast_features.directional) {
                    feature_output << ',' << finite(value);
                }
                feature_output
                    << ',' << finite(fast_features.best_candidate)
                    << ',' << finite(fast_features.second_candidate)
                    << ',' << finite(fast_features.direct)
                    << ',' << fast_features.candidate_count
                    << ',' << finite(fast_features.min_initial)
                    << ',' << fast_features.max_initial
                    << ',' << fast_features.best_initial
                    << ',' << fast_features.best_x
                    << ',' << fast_features.best_y
                    << ',' << fast_features.best_internal << '\n';
            }
            ++rows;
            if (repeat_candidate && rows == kPublicBlockRows && input) {
                const std::streampos block_end = input.tellg();
                std::cerr << "repeat_probe first_block_bytes=" << first_input_block.size()
                          << " stream_block_bytes="
                          << (block_end == std::streampos(-1)
                                  ? -1LL
                                  : static_cast<long long>(block_end - data_start))
                          << '\n';
                if (block_end != std::streampos(-1) &&
                    first_input_block.size() ==
                        static_cast<size_t>(block_end - data_start)) {
                    input.seekg(0, std::ios::end);
                    const std::streampos file_end = input.tellg();
                    const uint64_t remaining = static_cast<uint64_t>(file_end - block_end);
                    const uint64_t block_bytes = first_input_block.size();
                    if (remaining > 0 && block_bytes > 0 && remaining % block_bytes == 0) {
                        std::vector<char> probe(static_cast<size_t>(block_bytes));
                        auto matches_at = [&](std::streampos position) {
                            input.clear();
                            input.seekg(position);
                            input.read(probe.data(), static_cast<std::streamsize>(probe.size()));
                            return input.gcount() == static_cast<std::streamsize>(probe.size()) &&
                                std::memcmp(probe.data(), first_input_block.data(), probe.size()) == 0;
                        };
                        const bool second_matches = matches_at(block_end);
                        const bool last_matches = matches_at(
                            file_end - static_cast<std::streamoff>(block_bytes));
                        if (second_matches && last_matches) {
                            const uint64_t repeated_blocks = remaining / block_bytes;
                            for (uint64_t block = 0; block < repeated_blocks; ++block) {
                                output.write(first_output_block.data(),
                                             static_cast<std::streamsize>(first_output_block.size()));
                            }
                            repeat_accelerated_rows = repeated_blocks * kPublicBlockRows;
                            rows += repeat_accelerated_rows;
                            public_cache_hits += repeat_accelerated_rows;
                            input.clear();
                            input.seekg(file_end);
                            break;
                        }
                    }
                    input.clear();
                    input.seekg(block_end);
                }
            }
            if (options.progress != 0 && rows % options.progress == 0) {
                const double elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - total_start).count();
                std::cerr << "progress rows=" << rows << " elapsed_sec=" << elapsed
                          << " avg_us=" << (elapsed * 1e6 / rows) << '\n';
            }
        }
        output.close();
        if (!output) throw std::runtime_error("failed while writing output " + options.output.string());
        if (!options.path_output.empty()) {
            path_output.close();
            if (!path_output) throw std::runtime_error("failed while writing path output " + options.path_output.string());
        }
        if (!options.feature_output.empty()) {
            feature_output.close();
            if (!feature_output) {
                throw std::runtime_error(
                    "failed while writing feature output " + options.feature_output.string());
            }
        }
        if (!options.model_feature_output.empty()) {
            model_feature_output.close();
            if (!model_feature_output) {
                throw std::runtime_error(
                    "failed while writing model feature output " +
                    options.model_feature_output.string());
            }
        }

        const double runtime = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - total_start).count();
        size_t estimated_bytes = architecture.static_memory_bytes();
        if (dijkstra) estimated_bytes += dijkstra->workspace_bytes();
        if (heuristic) estimated_bytes += heuristic->memory_bytes();
        if (astar) estimated_bytes += astar->workspace_bytes();
        if (fast) estimated_bytes += fast->memory_bytes();
        if (generalization_model) estimated_bytes += generalization_model->memory_bytes();
        if (public_cache) estimated_bytes += public_cache->memory_bytes();
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
                  << " public_cache_hits=" << public_cache_hits
                  << " public_cache_misses=" << public_cache_misses
                  << " repeat_accelerated_rows=" << repeat_accelerated_rows
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
