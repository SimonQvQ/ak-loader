#pragma once
#include <cstdint>
#include <limits>

namespace akl {
constexpr int64_t kFpMax = std::numeric_limits<int64_t>::max() - 1;
constexpr int64_t kFpMin = -kFpMax;
// SideType constants were read from this game's enum metadata.
constexpr bool eligible_damage(bool is_damage, int source_side, int target_side, int64_t delta) {
    return is_damage && source_side == 1 && target_side == 2 && delta < 0 && delta >= kFpMin;
}
inline int64_t scale_damage(int64_t delta, int multiplier_x10) {
    if (delta < kFpMin || delta > kFpMax)
        return delta; // Preserve NaN/infinity sentinels.
    const __int128 scaled = static_cast<__int128>(delta) * multiplier_x10 / 10;
    if (scaled < kFpMin)
        return kFpMin;
    if (scaled > kFpMax)
        return kFpMax;
    return static_cast<int64_t>(scaled);
}
constexpr int protected_life_delta(int delta, bool player_side) {
    return player_side && delta < 0 ? 0 : delta;
}
constexpr float battle_scale(float requested, int multiplier_x100, bool in_battle) {
    return in_battle && requested > 0 ? requested * multiplier_x100 / 100.f : requested;
}
constexpr int winning_result(int original) {
    return original == 2 ? 1 : original;
}
} // namespace akl
