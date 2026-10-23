#include "battle_policy.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
int main() {
    constexpr int64_t one = int64_t{1} << 32;
    assert(akl::eligible_damage(true, 1, 2, -one));
    assert(!akl::eligible_damage(false, 1, 2, -one));
    assert(!akl::eligible_damage(true, 2, 1, -one));
    assert(!akl::eligible_damage(true, 1, 1, -one));
    assert(!akl::eligible_damage(true, 4, 2, -one));
    assert(!akl::eligible_damage(true, 1, 2, one));
    assert(!akl::eligible_damage(true, 1, 2, 0));
    assert(akl::scale_damage(-one * 100, 20) == -one * 200);
    assert(akl::scale_damage(-one * 100, 15) == -one * 150);
    assert(akl::scale_damage(-one * 100, 10) == -one * 100);
    assert(akl::scale_damage(akl::kFpMin, 200) == akl::kFpMin);
    assert(!akl::eligible_damage(true, 1, 2, std::numeric_limits<int64_t>::min()));
    assert(!akl::eligible_damage(true, 1, 2, -std::numeric_limits<int64_t>::max()));
    assert(akl::scale_damage(std::numeric_limits<int64_t>::min(), 20) ==
           std::numeric_limits<int64_t>::min());
    assert(akl::scale_damage(akl::kFpMax, 200) == akl::kFpMax);
    assert(akl::protected_life_delta(-1, true) == 0);
    assert(akl::protected_life_delta(1, true) == 1);
    assert(akl::protected_life_delta(-1, false) == -1);
    assert(akl::battle_scale(0, 200, true) == 0); // Pausing must remain possible.
    assert(akl::battle_scale(2, 200, true) == 4);
    assert(akl::battle_scale(1, 200, false) == 1);
    assert(akl::winning_result(0) == 0); // Do not finish a running battle.
    assert(akl::winning_result(2) == 1);
    std::cout << "battle policy checks passed (no game execution)\n";
}
