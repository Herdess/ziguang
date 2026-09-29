#include "prediction_cache.hpp"

#include <stdexcept>

namespace srb {

PredictionCache::PredictionCache(uint32_t port_count, size_t capacity_power)
    : port_count_(port_count) {
    if (port_count == 0 || capacity_power < 10 || capacity_power > 30) {
        throw std::runtime_error("invalid prediction cache configuration");
    }
    const size_t capacity = size_t{1} << capacity_power;
    mask_ = capacity - 1;
    insertion_limit_ = capacity * 7 / 10;
    keys_.assign(capacity, 0);
    delays_.resize(capacity);
}

uint64_t PredictionCache::key(const Pin& source, const Pin& target) const {
    const uint64_t source_id =
        static_cast<uint64_t>(source.site) * port_count_ + source.port;
    const uint64_t target_id =
        static_cast<uint64_t>(target.site) * port_count_ + target.port;
    return ((source_id << 32U) | target_id) + 1U;
}

uint64_t PredictionCache::hash(uint64_t value) {
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

bool PredictionCache::lookup(
    const Pin& source, const Pin& target, uint32_t& delay) const {
    const uint64_t wanted = key(source, target);
    size_t slot = static_cast<size_t>(hash(wanted)) & mask_;
    for (;;) {
        const uint64_t stored = keys_[slot];
        if (stored == 0) return false;
        if (stored == wanted) {
            delay = delays_[slot];
            return true;
        }
        slot = (slot + 1) & mask_;
    }
}

void PredictionCache::insert(
    const Pin& source, const Pin& target, uint32_t delay) {
    if (size_ >= insertion_limit_) return;
    const uint64_t wanted = key(source, target);
    size_t slot = static_cast<size_t>(hash(wanted)) & mask_;
    for (;;) {
        if (keys_[slot] == 0) {
            delays_[slot] = delay;
            keys_[slot] = wanted;
            ++size_;
            return;
        }
        if (keys_[slot] == wanted) {
            delays_[slot] = delay;
            return;
        }
        slot = (slot + 1) & mask_;
    }
}

size_t PredictionCache::memory_bytes() const {
    return keys_.capacity() * sizeof(uint64_t) +
           delays_.capacity() * sizeof(uint32_t);
}

}  // namespace srb
