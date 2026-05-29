#pragma once

/// @file
/// @brief Structural bridge to dimval: the `DimensionalValue` concept,
///        `NormalizedValue`, and the `normalize()` overloads.
///
/// prom never includes a dimval header. The `DimensionalValue` concept matches
/// `dimval::UnitValue` / `dimval::MeasureValue` purely *structurally* (duck
/// typing on `value_t`, `numeric_as_double()`, and a descriptor accessor), so a
/// consumer can hand prom dimensional values without prom taking a build-time
/// dependency on dimval's concrete types.

#include <prom/unit.hpp>

#include <concepts>
#include <type_traits>

namespace prom {

/// Satisfied by a dimval-like value: it exposes a `value_t`, reports its
/// magnitude through `numeric_as_double()`, and carries unit metadata through
/// either `unit_descriptor()` (measures) or `descriptor()` (units). Arithmetic
/// types are excluded so the arithmetic `normalize()` overload wins for them.
template <class V>
concept DimensionalValue =
    !std::is_arithmetic_v<std::remove_cvref_t<V>> &&
    requires(const std::remove_cvref_t<V>& value) {
        typename std::remove_cvref_t<V>::value_t;
        { value.numeric_as_double() } -> std::convertible_to<double>;
    } &&
    (requires(const std::remove_cvref_t<V>& value) { value.unit_descriptor(); } ||
     requires(const std::remove_cvref_t<V>& value) { value.descriptor(); });

/// A plain magnitude paired with the unit it was carrying (empty for raw
/// arithmetic). This is the common currency every metric mutation speaks.
struct NormalizedValue {
    double value = 0.0;
    Unit unit{};
};

/// Reduce a dimval value to a `NormalizedValue`. Measures expose both
/// `descriptor()` (the measure descriptor) and `unit_descriptor()` (the base
/// unit); units expose only `descriptor()` (which already *is* a unit
/// descriptor). Either way we want the unit descriptor's `long_name`, `kind`,
/// and `symbol`, so prefer `unit_descriptor()` when present.
template <DimensionalValue V>
[[nodiscard]] NormalizedValue normalize(const V& value) {
    NormalizedValue out;
    out.value = static_cast<double>(value.numeric_as_double());
    if constexpr (requires { value.unit_descriptor(); }) {
        const auto desc = value.unit_descriptor();
        out.unit = Unit{desc.long_name, desc.kind, desc.symbol, true};
    } else {
        const auto desc = value.descriptor();
        out.unit = Unit{desc.long_name, desc.kind, desc.symbol, true};
    }
    return out;
}

/// Pass an arithmetic sample straight through with no unit attached.
template <class T>
    requires std::is_arithmetic_v<T>
[[nodiscard]] NormalizedValue normalize(T value) noexcept {
    return NormalizedValue{static_cast<double>(value), Unit{}};
}

}  // namespace prom
