#pragma once

#include "architecture.hpp"
#include "fast_estimator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <string>
#include <vector>

namespace srb {

inline constexpr size_t kGeneralizationFeatureCount = 105;
using GeneralizationFeatureArray = std::array<int32_t, kGeneralizationFeatureCount>;

class GeneralizationFeatures {
public:
    explicit GeneralizationFeatures(const Architecture& architecture);

    GeneralizationFeatureArray build(
        const Pin& source, const Pin& target,
        const FastEstimateFeatures& search) const;

private:
    struct PortMetadata {
        uint16_t family = 0;
        uint16_t lane = 0;
        uint8_t domain = 0;
        uint8_t wire = 0;
        uint8_t compass = 0;
        uint8_t variant = 0;
        uint8_t bank = 0;
        uint16_t suffix = 0;
    };

    const Architecture& arch_;
    std::vector<PortMetadata> ports_;
};

class GeneralizationModel {
public:
    explicit GeneralizationModel(const std::filesystem::path& path);
    GeneralizationModel(const uint8_t* data, size_t size);

    double predict_residual(const GeneralizationFeatureArray& features) const;
    uint32_t predict(uint32_t raw, const GeneralizationFeatureArray& features) const;
    void predict_batch(const GeneralizationFeatureArray* features,
                       const uint32_t* raw, uint32_t* output,
                       size_t row_count, uint32_t workers) const;
    size_t memory_bytes() const;
    uint32_t tree_count() const { return static_cast<uint32_t>(roots_.size()); }

private:
    struct Node {
        int32_t left = 0;
        int32_t right = 0;
        int32_t decision = 0;
        uint16_t category_count = 0;
        uint8_t feature = 0;
        uint8_t categorical = 0;
    };

    std::vector<uint32_t> roots_;
    std::vector<Node> nodes_;
    std::vector<double> leaves_;
    std::vector<uint16_t> categories_;
    std::vector<uint64_t> category_masks_;
    uint32_t format_version_ = 0;

    void load(std::istream& input);
    double predict_tree(uint32_t root,
                        const GeneralizationFeatureArray& features) const;
};

struct EmbeddedGeneralizationModel {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

EmbeddedGeneralizationModel embedded_generalization_model();

}  // namespace srb
