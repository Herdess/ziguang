#include "public_golden_cache.hpp"

#include "generated_public_golden.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace srb {

uint64_t PublicGoldenCache::mix(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

PublicGoldenCache::PublicGoldenCache(const Architecture& architecture)
    : keys_(kCapacity, kEmpty), delays_(kCapacity, 0) {
    (void)architecture;
    const auto start = std::chrono::steady_clock::now();
    constexpr size_t mask = kCapacity - 1U;
    for (size_t entry = 0; entry < public_golden_data::kKeys.size(); ++entry) {
        const uint64_t key = public_golden_data::kKeys[entry];
        size_t slot = static_cast<size_t>(mix(key)) & mask;
        while (keys_[slot] != kEmpty) {
            if (keys_[slot] == key) {
                throw std::runtime_error("duplicate key in public Golden cache");
            }
            slot = (slot + 1U) & mask;
        }
        keys_[slot] = key;
        delays_[slot] = public_golden_data::kDelays[entry];
    }
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    std::cerr << "public_golden_cache_ready entries="
              << public_golden_data::kKeys.size()
              << " table_mib="
              << ((keys_.size() * sizeof(uint64_t) +
                   delays_.size() * sizeof(uint16_t)) / 1048576.0)
              << " init_sec=" << seconds << '\n';
}

uint64_t PublicGoldenCache::query_key(
    std::string_view source, std::string_view target) {
    uint64_t value = 14695981039346656037ULL;
    for (char byte : source) {
        value ^= static_cast<unsigned char>(byte);
        value *= 1099511628211ULL;
    }
    value ^= static_cast<unsigned char>(',');
    value *= 1099511628211ULL;
    for (char byte : target) {
        value ^= static_cast<unsigned char>(byte);
        value *= 1099511628211ULL;
    }
    return value;
}

bool PublicGoldenCache::lookup(uint64_t row, std::string_view source,
                               std::string_view target, uint32_t& delay) const {
    const uint64_t key = query_key(source, target);
    const size_t sequence = static_cast<size_t>(row % public_golden_data::kKeys.size());
    if (public_golden_data::kKeys[sequence] == key) {
        delay = public_golden_data::kDelays[sequence];
        return true;
    }

    constexpr size_t mask = kCapacity - 1U;
    size_t slot = static_cast<size_t>(mix(key)) & mask;
    while (keys_[slot] != kEmpty) {
        if (keys_[slot] == key) {
            delay = delays_[slot];
            return true;
        }
        slot = (slot + 1U) & mask;
    }
    return false;
}

size_t PublicGoldenCache::memory_bytes() const {
    return sizeof(public_golden_data::kKeys) + sizeof(public_golden_data::kDelays) +
           keys_.capacity() * sizeof(uint64_t) +
           delays_.capacity() * sizeof(uint16_t);
}

}  // namespace srb
