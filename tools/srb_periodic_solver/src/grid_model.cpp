#include "grid_model.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdexcept>

namespace grid16 {

Model::Model(const std::filesystem::path& path) {
#ifdef _WIN32
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("cannot open grid model: " + path.string());
    }
    file_handle_ = file;
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file, &size) || size.QuadPart < static_cast<LONGLONG>(sizeof(Header)) ||
        static_cast<unsigned long long>(size.QuadPart) >
            static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        ::CloseHandle(file);
        file_handle_ = nullptr;
        throw std::runtime_error("cannot stat or truncated grid model");
    }
    mapped_size_ = static_cast<size_t>(size.QuadPart);
    HANDLE mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) {
        ::CloseHandle(file);
        file_handle_ = nullptr;
        throw std::runtime_error("cannot create grid file mapping");
    }
    mapping_handle_ = mapping;
    mapping_ = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!mapping_) {
        ::CloseHandle(mapping);
        ::CloseHandle(file);
        mapping_handle_ = nullptr;
        file_handle_ = nullptr;
        throw std::runtime_error("cannot map grid model");
    }
#else
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) throw std::runtime_error("cannot open grid model: " + path.string());
    struct stat info{};
    if (::fstat(fd_, &info) != 0 || info.st_size < static_cast<off_t>(sizeof(Header))) {
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("cannot stat or truncated grid model");
    }
    mapped_size_ = static_cast<size_t>(info.st_size);
    mapping_ = ::mmap(nullptr, mapped_size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("cannot mmap grid model");
    }
#endif
    header_ = static_cast<const Header*>(mapping_);
    validate(*header_);
    const auto* bytes = static_cast<const uint8_t*>(mapping_);
    const uint64_t source_end = header_->source_ports_offset +
        static_cast<uint64_t>(header_->source_count) * sizeof(uint16_t);
    const uint64_t target_end = header_->target_ports_offset +
        static_cast<uint64_t>(header_->target_count) * sizeof(uint16_t);
    const uint64_t values_end = header_->values_offset + header_->values_bytes;
    if (source_end > mapped_size_ || target_end > mapped_size_ || values_end > mapped_size_) {
        throw std::runtime_error("grid sections exceed file size");
    }
    source_ports_ = reinterpret_cast<const uint16_t*>(bytes + header_->source_ports_offset);
    target_ports_ = reinterpret_cast<const uint16_t*>(bytes + header_->target_ports_offset);
    values_ = reinterpret_cast<const uint16_t*>(bytes + header_->values_offset);

    uint16_t max_port = 0;
    for (uint32_t i = 0; i < header_->source_count; ++i) max_port = std::max(max_port, source_ports_[i]);
    for (uint32_t i = 0; i < header_->target_count; ++i) max_port = std::max(max_port, target_ports_[i]);
    source_index_.assign(static_cast<size_t>(max_port) + 1U, -1);
    target_index_.assign(static_cast<size_t>(max_port) + 1U, -1);
    for (uint32_t i = 0; i < header_->source_count; ++i) source_index_[source_ports_[i]] = static_cast<int16_t>(i);
    for (uint32_t i = 0; i < header_->target_count; ++i) target_index_[target_ports_[i]] = static_cast<int16_t>(i);
    for (int dx = -119; dx <= 119; ++dx) {
        x_desc_[static_cast<size_t>(dx + 119)] = make_axis_desc(dx, -119, 119, 10, 4);
    }
    for (int dy = -549; dy <= 549; ++dy) {
        y_desc_[static_cast<size_t>(dy + 549)] = make_axis_desc(dy, -549, 549, 12, 8);
    }
}

Model::~Model() {
#ifdef _WIN32
    if (mapping_) ::UnmapViewOfFile(mapping_);
    if (mapping_handle_) ::CloseHandle(static_cast<HANDLE>(mapping_handle_));
    if (file_handle_) ::CloseHandle(static_cast<HANDLE>(file_handle_));
#else
    if (mapping_) ::munmap(mapping_, mapped_size_);
    if (fd_ >= 0) ::close(fd_);
#endif
}

bool Model::has_source(uint16_t port) const {
    return port < source_index_.size() && source_index_[port] >= 0;
}

bool Model::has_target(uint16_t port) const {
    return port < target_index_.size() && target_index_[port] >= 0;
}

uint16_t Model::lookup(uint16_t source_port, uint16_t target_port, int dx, int dy) const {
    if (dx < -119 || dx > 119 || dy < -549 || dy > 549 ||
        !has_source(source_port) || !has_target(target_port)) return kUnreachable;
    const AxisDesc& xd = x_desc_[static_cast<size_t>(dx + 119)];
    const AxisDesc& yd = y_desc_[static_cast<size_t>(dy + 549)];
    const uint64_t source = static_cast<uint16_t>(source_index_[source_port]);
    const uint64_t target = static_cast<uint16_t>(target_index_[target_port]);
    const uint64_t block_index = (((source * header_->target_count + target) * kYResidues +
        yd.residue) * kXResidues + xd.residue);
    const uint16_t* block = values_ + block_index * kBlockValues;
    const uint16_t v00 = block[static_cast<uint32_t>(yd.lower_node) * kXNodes + xd.lower_node];
    const uint16_t v10 = block[static_cast<uint32_t>(yd.lower_node) * kXNodes + xd.upper_node];
    const uint16_t v01 = block[static_cast<uint32_t>(yd.upper_node) * kXNodes + xd.lower_node];
    const uint16_t v11 = block[static_cast<uint32_t>(yd.upper_node) * kXNodes + xd.upper_node];
    return bilinear_q16(v00, v10, v01, v11, xd.weight_q16, yd.weight_q16);
}

}  // namespace grid16
