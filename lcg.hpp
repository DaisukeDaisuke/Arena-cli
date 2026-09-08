#pragma once
#include <cstdint>
#include <limits>
#include <random>
namespace arena {
class Lcg64 {
    std::uint64_t state_;
public:
    explicit constexpr Lcg64(std::uint64_t seed) noexcept : state_(seed) {}
    constexpr std::uint64_t next() noexcept {
        state_ = state_ * UINT64_C(0x5d588b656c078965) + UINT64_C(0x269ec3);
        return state_;
    }
    constexpr std::uint32_t percent() noexcept {
        return static_cast<std::uint32_t>(((next() >> 32) * UINT64_C(100)) >> 32);
    }
    constexpr std::uint64_t state() const noexcept { return state_; }
};
inline std::uint64_t device_seed() {
    static_assert(std::numeric_limits<unsigned int>::digits == 32);
    std::random_device device;
    const auto hi = static_cast<std::uint64_t>(device());
    const auto lo = static_cast<std::uint64_t>(device());
    return (hi << 32) | lo;
}
}
