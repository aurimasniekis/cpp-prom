#pragma once

/// @file
/// @brief Metric kinds (`MetricType`) and the OpenMetrics `Unit` descriptor.

#include <cstdint>
#include <string_view>

namespace prom {

/// The OpenMetrics / Prometheus metric kinds prom understands.
///
/// The numeric values are stable and intended for cheap switch dispatch inside
/// adapters; do not rely on them crossing an ABI boundary.
enum class MetricType : std::uint8_t {
    Counter,    ///< Monotonically increasing cumulative value.
    Gauge,      ///< Value that can go up and down.
    Histogram,  ///< Bucketed distribution of observations.
    Summary,    ///< Quantile summary of observations.
    Untyped,    ///< A bare value with no semantic constraints.
    Info,       ///< Static key/value metadata (exposed as `*_info`).
    StateSet,   ///< A set of mutually-related boolean states.
};

/// Lower-case OpenMetrics spelling of a `MetricType` (e.g. `"counter"`).
[[nodiscard]] constexpr std::string_view to_string(const MetricType type) noexcept {
    switch (type) {
    case MetricType::Counter:
        return "counter";
    case MetricType::Gauge:
        return "gauge";
    case MetricType::Histogram:
        return "histogram";
    case MetricType::Summary:
        return "summary";
    case MetricType::Untyped:
        return "untyped";
    case MetricType::Info:
        return "info";
    case MetricType::StateSet:
        return "stateset";
    }
    return "unknown";
}

/// OpenMetrics unit suffix plus optional dimensional metadata.
///
/// All string fields are `std::string_view` so that the built-in dimval
/// descriptors — which return `string_view` literals — stay zero-copy. When a
/// `Unit` is handed to an `Adapter` (via `MetricMeta` or `Adapter::set_unit`)
/// the views must stay valid only for the duration of that call; backends copy
/// what they need synchronously.
struct Unit {
    std::string_view name{};    ///< Human-readable unit name (e.g. `"seconds"`).
    std::string_view kind{};    ///< Dimensional compatibility group (e.g. `"time"`).
    std::string_view symbol{};  ///< Display symbol (e.g. `"s"`).
    bool from_dimval = false;   ///< True when inferred from a dimval value.

    /// A unit carries no information when it has neither a name nor a kind.
    [[nodiscard]] constexpr bool empty() const noexcept {
        return name.empty() && kind.empty();
    }

    bool operator==(const Unit&) const = default;
};

}  // namespace prom
