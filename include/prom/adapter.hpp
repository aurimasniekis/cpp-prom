#pragma once

/// @file
/// @brief The backend boundary: `MetricMeta`, `MetricState`/`MetricHandle`, and
///        the pure-virtual `Adapter` interface.
///
/// Everything below the metric types funnels through this one interface. A
/// backend (the bundled `NullAdapter`, or e.g. the optional prometheus-cpp
/// module) subclasses `Adapter`, and prom never lets a backend-specific type
/// leak across this line. The model is **register-then-mutate**: a family is
/// registered once, labeled children are obtained with `resolve`, and samples
/// are pushed with `inc`/`dec`/`set`/`observe`.

#include <prom/fwd.hpp>
#include <prom/labels.hpp>
#include <prom/unit.hpp>

#include <commons/display_info.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace prom {

/// The complete, backend-agnostic description of a metric family handed to
/// `Adapter::register_metric`. The `string_view`/`span` members reference
/// storage owned by the caller (the metric's shared state) and are guaranteed
/// valid only for the duration of the `register_metric` call — backends must
/// copy anything they need to keep.
struct MetricMeta {
    MetricType type{};                      ///< Which kind of metric this is.
    std::string_view name{};                ///< Fully-qualified metric name.
    std::string_view help{};                ///< One-line description.
    Unit unit{};                            ///< Declared unit (may be empty).
    Labels const_labels{};                  ///< Labels fixed for the whole family.
    comms::DisplayInfo display{};           ///< Optional UI metadata.
    std::span<const double> buckets{};      ///< Histogram bucket bounds (else empty).
    std::span<const double> quantiles{};    ///< Summary quantiles (else empty).
    std::span<const std::string> states{};  ///< State-set member names (else empty).
};

/// Opaque backend state for a registered family or a labeled child. Backends
/// subclass this to stash whatever they need (a prometheus-cpp `Family<T>&`, a
/// child reference, ...). prom only ever holds it behind a `MetricHandle`.
class MetricState {
public:
    MetricState() = default;
    MetricState(const MetricState&) = default;
    MetricState& operator=(const MetricState&) = default;
    MetricState(MetricState&&) = default;
    MetricState& operator=(MetricState&&) = default;
    virtual ~MetricState() = default;
};

/// The pluggable backend. Non-copyable, non-movable: an `Adapter` is owned
/// through a `shared_ptr` and referenced by every metric bound to it.
///
/// **Threading contract.** Every method may be called concurrently from any
/// thread. Backends are responsible for their own synchronization. No method
/// throws — failures are absorbed (and ideally logged) by the backend.
class Adapter {
public:
    Adapter() = default;
    Adapter(const Adapter&) = delete;
    Adapter& operator=(const Adapter&) = delete;
    Adapter(Adapter&&) = delete;
    Adapter& operator=(Adapter&&) = delete;
    virtual ~Adapter() = default;

    /// Stable identifier of the backend (e.g. `"null"`, `"prometheus-cpp"`).
    [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;

    /// Register a metric family. Must return a non-null handle even on failure
    /// (fall back to an inert handle) so callers never have to null-check.
    [[nodiscard]] virtual MetricHandle register_metric(const MetricMeta& meta) noexcept = 0;

    /// Resolve the labeled child of `family` for the given dynamic labels,
    /// creating it on first request. The backend owns the child cache. Must
    /// return a non-null handle.
    [[nodiscard]] virtual MetricHandle resolve(const MetricHandle& family,
                                               const Labels& dynamic) noexcept = 0;

    /// Increase the series behind `handle` by `amount` (counter/gauge).
    virtual void inc(const MetricHandle& handle, double amount) noexcept = 0;
    /// Decrease the series behind `handle` by `amount` (gauge).
    virtual void dec(const MetricHandle& handle, double amount) noexcept = 0;
    /// Set the series behind `handle` to `value` (gauge/untyped).
    virtual void set(const MetricHandle& handle, double value) noexcept = 0;
    /// Record an observation against `handle` (histogram/summary).
    virtual void observe(const MetricHandle& handle, double value) noexcept = 0;

    /// Replace the label set carried by an info metric.
    virtual void set_info(const MetricHandle& handle, std::span<const Label> labels) noexcept = 0;
    /// Set the boolean value of one member of a state set.
    virtual void
    set_state(const MetricHandle& handle, std::string_view state, bool active) noexcept = 0;

    /// Late unit inference hook. Called when a metric that declared no unit
    /// latches the unit of its first dimensional observation. Optional —
    /// backends that cannot rename a registered series may ignore it.
    virtual void set_unit(const MetricHandle& /*handle*/, const Unit& /*unit*/) noexcept {}
};

/// The source a metric reads its current adapter from, decoupled from
/// `Registry` so that `metric_base` can resolve a binding without depending on
/// the registry layer (which would be an include cycle), mirroring `ScopeState`.
///
/// Every `MetricCore` holds a `shared_ptr<AdapterSource>` — the `AdapterCell` of
/// the `Registry` that created it, or the process-wide global cell for
/// standalone and scoped metrics. `version()` advances whenever the adapter is
/// swapped, which is how an already-created metric notices it must re-register
/// against the new backend on its next use (live migration).
class AdapterSource {
public:
    AdapterSource() = default;
    AdapterSource(const AdapterSource&) = delete;
    AdapterSource& operator=(const AdapterSource&) = delete;
    AdapterSource(AdapterSource&&) = delete;
    AdapterSource& operator=(AdapterSource&&) = delete;
    virtual ~AdapterSource() = default;

    /// Monotonic counter, bumped on every adapter swap.
    [[nodiscard]] virtual std::uint64_t version() const noexcept = 0;

    /// The current adapter, never null. Returns a `shared_ptr` copy so a
    /// concurrent swap cannot invalidate the adapter an in-flight caller uses.
    [[nodiscard]] virtual AdapterPtr adapter() const = 0;
};

}  // namespace prom
