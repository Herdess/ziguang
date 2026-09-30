#pragma once

#include "grid_format.hpp"
#include "grid_math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace grid16 {

class Model {
public:
    explicit Model(const std::filesystem::path& path);
    ~Model();
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    uint16_t lookup(uint16_t source_port, uint16_t target_port, int dx, int dy) const;
    bool has_source(uint16_t port) const;
    bool has_target(uint16_t port) const;
    uint32_t source_count() const { return header_->source_count; }
    uint32_t target_count() const { return header_->target_count; }
    size_t mapped_bytes() const { return mapped_size_; }
    const Header& header() const { return *header_; }
    const uint16_t* source_ports() const { return source_ports_; }
    const uint16_t* target_ports() const { return target_ports_; }
    const uint16_t* values() const { return values_; }

private:
#ifdef _WIN32
    void* file_handle_ = nullptr;
    void* mapping_handle_ = nullptr;
#else
    int fd_ = -1;
#endif
    void* mapping_ = nullptr;
    size_t mapped_size_ = 0;
    const Header* header_ = nullptr;
    const uint16_t* source_ports_ = nullptr;
    const uint16_t* target_ports_ = nullptr;
    const uint16_t* values_ = nullptr;
    std::vector<int16_t> source_index_;
    std::vector<int16_t> target_index_;
    std::array<AxisDesc, 239> x_desc_{};
    std::array<AxisDesc, 1099> y_desc_{};
};

}  // namespace grid16
