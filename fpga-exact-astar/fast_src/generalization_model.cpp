#include "generalization_model.hpp"

#include "generated_arch_data.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace srb {
namespace {

class MemoryBuffer : public std::streambuf {
public:
    MemoryBuffer(const uint8_t* data, size_t size) {
        char* begin = reinterpret_cast<char*>(const_cast<uint8_t*>(data));
        setg(begin, begin, begin + size);
    }
};

template <typename T>
T read_value(std::istream& input, const char* label) {
    T value{};
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!input) throw std::runtime_error(std::string("truncated model while reading ") + label);
    return value;
}

template <typename T>
void read_values(std::istream& input, std::vector<T>& values, const char* label) {
    if (values.empty()) return;
    input.read(reinterpret_cast<char*>(values.data()),
               static_cast<std::streamsize>(values.size() * sizeof(T)));
    if (!input) throw std::runtime_error(std::string("truncated model while reading ") + label);
}

int sign_of(int value) { return (value > 0) - (value < 0); }

int category_id(char value, std::string_view alphabet) {
    const size_t position = alphabet.find(value);
    if (position == std::string_view::npos) {
        throw std::runtime_error("unexpected port-name category");
    }
    return static_cast<int>(position);
}

int32_t search_feature(uint32_t value) {
    return value == kInfinity ? -1 : static_cast<int32_t>(value);
}

constexpr std::array<int, 14> kVerticalSites = {
    9, 19, 33, 39, 45, 51, 57, 63, 69, 95, 99, 105, 111, 117};
constexpr std::array<int, 14> kVerticalDelays = {
    78, 78, 9, 78, 78, 78, 98, 9, 78, 98, 177, 9, 78, 9};
constexpr std::array<int, 5> kHorizontalSites = {49, 149, 249, 349, 449};
constexpr std::array<int, 5> kHorizontalDelays = {8, 8, 8, 8, 8};

}  // namespace

GeneralizationFeatures::GeneralizationFeatures(const Architecture& architecture)
    : arch_(architecture) {
    ports_.resize(generated::kPortNames.size());
    std::unordered_map<std::string, uint16_t> family_ids;
    std::unordered_map<std::string, uint16_t> suffix_ids;
    for (size_t index = 0; index < generated::kPortNames.size(); ++index) {
        const std::string name(generated::kPortNames[index]);
        const size_t bracket = name.rfind('[');
        const std::string family = bracket == std::string::npos
            ? name : name.substr(0, bracket);
        auto family_result = family_ids.emplace(
            family, static_cast<uint16_t>(family_ids.size()));

        uint16_t lane = 0;
        if (bracket != std::string::npos && name.back() == ']') {
            lane = static_cast<uint16_t>(std::stoi(
                name.substr(bracket + 1, name.size() - bracket - 2)));
        }
        const bool zi = name[0] == 'Z' || name[0] == 'I';
        const bool as = name[0] == 'A' || name[0] == 'S';
        const std::string suffix = as ? name.substr(3) : "-";
        auto suffix_result = suffix_ids.emplace(
            suffix, static_cast<uint16_t>(suffix_ids.size()));

        PortMetadata& metadata = ports_[index];
        metadata.family = family_result.first->second;
        metadata.lane = lane;
        metadata.domain = static_cast<uint8_t>(category_id(name[0], "ZIAS"));
        metadata.wire = static_cast<uint8_t>(category_id(zi ? name[1] : '-', "SDQL-"));
        metadata.compass = static_cast<uint8_t>(category_id(zi ? name[2] : '-', "ENSW-"));
        metadata.variant = static_cast<uint8_t>(category_id(
            zi && family.size() == 4 ? name[3] : '-', "AB-"));
        metadata.bank = static_cast<uint8_t>(category_id(as ? name[2] : '-', "ABCDEFGH-"));
        metadata.suffix = suffix_result.first->second;
    }
    if (ports_.size() != arch_.port_count()) {
        throw std::runtime_error("generated port metadata does not match architecture");
    }
}

GeneralizationFeatureArray GeneralizationFeatures::build(
    const Pin& source, const Pin& target,
    const FastEstimateFeatures& search) const {
    GeneralizationFeatureArray values{};
    const int sx = arch_.site_x(source.site);
    const int sy = arch_.site_y(source.site);
    const int tx = arch_.site_x(target.site);
    const int ty = arch_.site_y(target.site);
    const int dx = tx - sx;
    const int dy = ty - sy;
    const int abs_dx = std::abs(dx);
    const int abs_dy = std::abs(dy);
    const int midpoint_x = (sx + tx) / 2;
    const int midpoint_y = (sy + ty) / 2;

    auto crossed = [](int a, int b, const auto& sites, const auto& delays,
                      uint32_t& delay, uint32_t& count, uint32_t& mask) {
        const int low = std::min(a, b);
        const int high = std::max(a, b);
        delay = count = mask = 0;
        for (size_t index = 0; index < sites.size(); ++index) {
            if (low <= sites[index] && sites[index] < high) {
                delay += static_cast<uint32_t>(delays[index]);
                ++count;
                mask |= 1U << index;
            }
        }
    };
    uint32_t vertical_delay = 0, vertical_count = 0, vertical_mask = 0;
    uint32_t horizontal_delay = 0, horizontal_count = 0, horizontal_mask = 0;
    crossed(sx, tx, kVerticalSites, kVerticalDelays,
            vertical_delay, vertical_count, vertical_mask);
    crossed(sy, ty, kHorizontalSites, kHorizontalDelays,
            horizontal_delay, horizontal_count, horizontal_mask);

    uint32_t blocks = 0;
    const int min_x = std::min(sx, tx);
    const int max_x = std::max(sx, tx);
    const int min_y = std::min(sy, ty);
    const int max_y = std::max(sy, ty);
    for (size_t index = 0; index < generated::kGapBlocks.size(); ++index) {
        const auto& block = generated::kGapBlocks[index];
        if (min_x <= block.right && max_x >= block.left &&
            min_y <= block.upper && max_y >= block.lower) {
            blocks |= 1U << index;
        }
    }

    auto zone = [](const auto& sites, int coordinate) {
        return static_cast<int>(std::lower_bound(
            sites.begin(), sites.end(), coordinate) - sites.begin());
    };
    auto nearest = [](const auto& sites, int coordinate) {
        int result = std::numeric_limits<int>::max();
        for (int site : sites) result = std::min(result, std::abs(coordinate - site));
        return result;
    };
    const int source_x_zone = zone(kVerticalSites, sx);
    const int target_x_zone = zone(kVerticalSites, tx);
    const int source_y_zone = zone(kHorizontalSites, sy);
    const int target_y_zone = zone(kHorizontalSites, ty);
    constexpr int y_zone_count = 6;
    const PortMetadata& source_metadata = ports_.at(source.port);
    const PortMetadata& target_metadata = ports_.at(target.port);
    const int source_y_mod100 = sy % 100;
    const int target_y_mod100 = ty % 100;
    const int midpoint_y_mod100 = midpoint_y % 100;
    auto x_side = [](int x) { return x < 76 ? 0 : (x <= 89 ? 1 : 2); };
    auto open_distance = [](int y_mod100) {
        return y_mod100 >= 50 ? 0 : std::min(50 - y_mod100, y_mod100 + 1);
    };

    size_t out = 0;
    auto add = [&](int64_t value) { values[out++] = static_cast<int32_t>(value); };
    add(sx); add(sy); add(tx); add(ty);
    add(dx); add(dy); add(abs_dx); add(abs_dy);
    add(abs_dx + abs_dy); add(std::max(abs_dx, abs_dy));
    add(source.port); add(target.port);
    add((sign_of(dx) + 1) * 3 + sign_of(dy) + 1);
    add(midpoint_x); add(midpoint_y);
    add(vertical_delay); add(horizontal_delay); add(vertical_count); add(horizontal_count);
    add(blocks);
    add(source_x_zone); add(source_y_zone); add(target_x_zone); add(target_y_zone);
    add(source_x_zone * y_zone_count + source_y_zone);
    add(target_x_zone * y_zone_count + target_y_zone);
    add(nearest(kVerticalSites, sx)); add(nearest(kVerticalSites, tx));
    add(nearest(kHorizontalSites, sy)); add(nearest(kHorizontalSites, ty));
    add(abs_dx % 10); add(abs_dy % 12); add(vertical_mask); add(horizontal_mask);
    add(source_metadata.family); add(target_metadata.family);
    add(source_metadata.lane); add(target_metadata.lane);
    add(source_metadata.domain); add(target_metadata.domain);
    add(source_metadata.wire); add(target_metadata.wire);
    add(source_metadata.compass); add(target_metadata.compass);
    add(source_metadata.variant); add(target_metadata.variant);
    add(source_metadata.bank); add(target_metadata.bank);
    add(source_metadata.suffix); add(target_metadata.suffix);
    add(source_y_mod100); add(target_y_mod100); add(midpoint_y_mod100);
    add(sy / 50); add(ty / 50); add(x_side(sx)); add(x_side(tx));
    add(((sx < 76 && tx > 89) || (tx < 76 && sx > 89)) ? 1 : 0);
    add(open_distance(source_y_mod100)); add(open_distance(target_y_mod100));
    for (size_t index = 0; index < kVerticalSites.size(); ++index) {
        add((vertical_mask >> index) & 1U);
    }
    for (size_t index = 0; index < kHorizontalSites.size(); ++index) {
        add((horizontal_mask >> index) & 1U);
    }
    for (size_t index = 0; index < generated::kGapBlocks.size(); ++index) {
        add((blocks >> index) & 1U);
    }
    add(search_feature(search.raw));
    for (uint32_t value : search.directional) add(search_feature(value));
    add(search_feature(search.best_candidate));
    add(search_feature(search.second_candidate));
    add(search_feature(search.direct));
    add(search.candidate_count);
    add(search_feature(search.min_initial));
    add(search.max_initial); add(search.best_initial);
    add(search.best_x); add(search.best_y); add(search.best_internal);
    if (out != values.size()) throw std::runtime_error("generalization feature count mismatch");
    return values;
}

GeneralizationModel::GeneralizationModel(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open generalization model " + path.string());
    load(input);
}

GeneralizationModel::GeneralizationModel(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        throw std::runtime_error("embedded generalization model is empty");
    }
    MemoryBuffer buffer(data, size);
    std::istream input(&buffer);
    load(input);
}

void GeneralizationModel::load(std::istream& input) {
    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    const bool version1_magic =
        std::memcmp(magic.data(), "SRBGBM1\0", magic.size()) == 0;
    const bool version2_magic =
        std::memcmp(magic.data(), "SRBGBM2\0", magic.size()) == 0;
    const bool version3_magic =
        std::memcmp(magic.data(), "SRBGBM3\0", magic.size()) == 0;
    if (!input || (!version1_magic && !version2_magic && !version3_magic)) {
        throw std::runtime_error("invalid generalization model magic");
    }
    const uint32_t version = read_value<uint32_t>(input, "version");
    format_version_ = version;
    const uint32_t feature_count = read_value<uint32_t>(input, "feature count");
    const uint32_t tree_count = read_value<uint32_t>(input, "tree count");
    const uint32_t node_count = read_value<uint32_t>(input, "node count");
    const uint32_t leaf_count = read_value<uint32_t>(input, "leaf count");
    const uint32_t category_count = read_value<uint32_t>(input, "category count");
    if ((version < 1 || version > 3) ||
        (version == 1) != version1_magic ||
        (version == 2) != version2_magic ||
        (version == 3) != version3_magic ||
        feature_count != kGeneralizationFeatureCount ||
        tree_count == 0 || node_count == 0 || leaf_count == 0) {
        throw std::runtime_error("unsupported generalization model header");
    }
    roots_.resize(tree_count);
    read_values(input, roots_, "tree roots");
    nodes_.resize(node_count);
    for (Node& node : nodes_) {
        node.left = read_value<int32_t>(input, "node left child");
        node.right = read_value<int32_t>(input, "node right child");
        if (version <= 2) {
            const double threshold = read_value<double>(input, "node threshold");
            const uint32_t category_offset =
                read_value<uint32_t>(input, "node category offset");
            node.category_count = read_value<uint16_t>(input, "node category count");
            node.feature = read_value<uint8_t>(input, "node feature");
            node.categorical = read_value<uint8_t>(input, "node type");
            node.decision = node.categorical
                ? static_cast<int32_t>(category_offset)
                : static_cast<int32_t>(std::floor(threshold));
        } else {
            node.decision = read_value<int32_t>(input, "node decision");
            node.category_count = read_value<uint16_t>(input, "node category count");
            node.feature = read_value<uint8_t>(input, "node feature");
            node.categorical = read_value<uint8_t>(input, "node type");
        }
    }
    leaves_.resize(leaf_count);
    read_values(input, leaves_, "leaves");
    if (version == 1) {
        categories_.resize(category_count);
        read_values(input, categories_, "categories");
    } else {
        category_masks_.resize(category_count);
        read_values(input, category_masks_, "category masks");
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("unexpected trailing bytes in generalization model");
    }
    for (uint32_t root : roots_) {
        if (root >= nodes_.size()) throw std::runtime_error("invalid model root");
    }
    for (const Node& node : nodes_) {
        auto valid_child = [&](int32_t child) {
            return child >= 0 ? static_cast<size_t>(child) < nodes_.size()
                              : static_cast<size_t>(-static_cast<int64_t>(child) - 1) < leaves_.size();
        };
        if (!valid_child(node.left) || !valid_child(node.right) ||
            node.feature >= kGeneralizationFeatureCount ||
            (node.categorical && (node.decision < 0 ||
             static_cast<uint64_t>(node.decision) + node.category_count >
                (version == 1 ? categories_.size() : category_masks_.size())))) {
            throw std::runtime_error("invalid generalization model node");
        }
    }
    if (version == 1) {
        // The compact on-disk representation stores sorted uint16 category
        // lists.  Expand them once at startup so hot inference uses one
        // constant-time bitmap lookup per categorical split.
        size_t total_words = 0;
        for (const Node& node : nodes_) {
            if (!node.categorical || node.category_count == 0) continue;
            total_words += static_cast<size_t>(categories_[
                static_cast<size_t>(node.decision) + node.category_count - 1]) / 64U + 1U;
        }
        category_masks_.reserve(total_words);
        for (Node& node : nodes_) {
            if (!node.categorical) continue;
            const size_t old_offset = static_cast<size_t>(node.decision);
            const size_t old_count = node.category_count;
            const uint32_t word_count = old_count == 0 ? 0U
                : static_cast<uint32_t>(categories_[old_offset + old_count - 1]) / 64U + 1U;
            const size_t new_offset = category_masks_.size();
            category_masks_.resize(new_offset + word_count, 0);
            for (size_t index = 0; index < old_count; ++index) {
                const uint16_t category = categories_[old_offset + index];
                category_masks_[new_offset + category / 64U] |=
                    uint64_t{1} << (category & 63U);
            }
            node.decision = static_cast<int32_t>(new_offset);
            node.category_count = static_cast<uint16_t>(word_count);
        }
        categories_.clear();
        categories_.shrink_to_fit();
        format_version_ = 2;
    }
}

double GeneralizationModel::predict_tree(
    uint32_t root, const GeneralizationFeatureArray& features) const {
    int32_t index = static_cast<int32_t>(root);
    while (index >= 0) {
        const Node& node = nodes_[static_cast<size_t>(index)];
        const int32_t value = features[node.feature];
        bool left = false;
        if (node.categorical) {
            const int32_t category = value;
            if (format_version_ == 1) {
                const auto begin = categories_.begin() + node.decision;
                left = category >= 0 && category <= UINT16_MAX &&
                    std::binary_search(
                        begin, begin + node.category_count,
                        static_cast<uint16_t>(category));
            } else if (category >= 0) {
                const uint32_t word = static_cast<uint32_t>(category) >> 6U;
                left = word < node.category_count &&
                    ((category_masks_[static_cast<size_t>(node.decision) + word] >>
                      (static_cast<uint32_t>(category) & 63U)) & 1U) != 0;
            }
        } else {
            left = value <= node.decision;
        }
        index = left ? node.left : node.right;
    }
    return leaves_[static_cast<size_t>(-static_cast<int64_t>(index) - 1)];
}

double GeneralizationModel::predict_residual(
    const GeneralizationFeatureArray& features) const {
    double total = 0.0;
    for (uint32_t root : roots_) total += predict_tree(root, features);
    return total;
}

uint32_t GeneralizationModel::predict(
    uint32_t raw, const GeneralizationFeatureArray& features) const {
    if (raw == 0 || raw == kInfinity) return raw;
    const double corrected = static_cast<double>(raw) * std::exp(predict_residual(features));
    if (!(corrected > 0.0)) return 0;
    if (corrected >= std::numeric_limits<uint32_t>::max()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(std::nearbyint(corrected));
}

void GeneralizationModel::predict_batch(
    const GeneralizationFeatureArray* features, const uint32_t* raw,
    uint32_t* output, size_t row_count, uint32_t workers) const {
    if (row_count == 0) return;
    workers = std::max<uint32_t>(1, std::min<uint32_t>(
        workers, static_cast<uint32_t>(row_count)));
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (uint32_t worker = 0; worker < workers; ++worker) {
        const size_t begin = row_count * worker / workers;
        const size_t end = row_count * (worker + 1U) / workers;
        threads.emplace_back([&, begin, end] {
            std::vector<double> residual(end - begin, 0.0);
            // Tree-major traversal keeps each tree's nodes and categorical
            // bitmaps hot while preserving tree-order summation per row.
            for (uint32_t root : roots_) {
                for (size_t row = begin; row < end; ++row) {
                    residual[row - begin] += predict_tree(root, features[row]);
                }
            }
            for (size_t row = begin; row < end; ++row) {
                if (raw[row] == 0 || raw[row] == kInfinity) {
                    output[row] = raw[row];
                    continue;
                }
                const double corrected = static_cast<double>(raw[row]) *
                    std::exp(residual[row - begin]);
                if (!(corrected > 0.0)) output[row] = 0;
                else if (corrected >= std::numeric_limits<uint32_t>::max()) {
                    output[row] = std::numeric_limits<uint32_t>::max();
                } else {
                    output[row] = static_cast<uint32_t>(std::nearbyint(corrected));
                }
            }
        });
    }
    for (std::thread& thread : threads) thread.join();
}

size_t GeneralizationModel::memory_bytes() const {
    return roots_.capacity() * sizeof(uint32_t) + nodes_.capacity() * sizeof(Node) +
           leaves_.capacity() * sizeof(double) + categories_.capacity() * sizeof(uint16_t) +
           category_masks_.capacity() * sizeof(uint64_t);
}

}  // namespace srb
