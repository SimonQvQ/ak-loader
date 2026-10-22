#pragma once
#include <cstdint>

namespace akl {
constexpr int32_t kGuestProbe = 0x414b4c31;
// Zero means at least one hook was installed; pending is not a battle result.
constexpr int kStarted = 0;
constexpr int kPending = 1;
constexpr int kWrongArchitecture = -10;
constexpr int kIl2cppUnavailable = -11;
constexpr int kCodeUnavailable = -12;
constexpr int kApiUnavailable = -13;
constexpr int kThreadUnavailable = -14;
constexpr int kNoHooks = -15;
constexpr int kBridgeUnavailable = -30;
constexpr int kNamespaceUnavailable = -31;
constexpr int kGuestLoadFailed = -32;
constexpr int kNotTranslated = -33;
constexpr int kProbeFailed = -34;
constexpr int kEntryUnavailable = -35;
constexpr int kInvalidGuest = -36;
}
