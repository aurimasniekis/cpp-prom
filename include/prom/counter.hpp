#pragma once

/// @file
/// @brief `Counter` — a monotonically increasing cumulative metric, and its
///        designated-initializable `CounterSpec`.

#include <prom/metric_base.hpp>

#include <string_view>
#include <utility>

namespace prom {

/// Declarative description of a counter, for `Registry::counter` /
/// `Counter::Counter`. Use designated initializers:
/// `CounterSpec{.name = "http_requests_total", .help = "..."}`.
struct CounterSpec {
    std::string_view name{};
    std::string_view help{};
    Labels labels{};
    Unit unit{};
    comms::DisplayInfo display{};
};

/// A counter. Only ever moves forward; negative or non-finite increments are
/// dropped and logged. Values may be raw arithmetic or dimensional (dimval)
/// quantities — both route through `normalize()`.
class Counter : public MetricBase<Counter> {
public:
    /// Standalone, unbound counter. Binds to the default adapter on first use.
    Counter(const std::string_view name, const std::string_view help)
        : MetricBase(MetricType::Counter, name, help) {}

    /// Standalone counter from a spec (unbound). Convenience mirror of the
    /// `Registry` factory for code that does not hold a registry.
    explicit Counter(const CounterSpec& spec)
        : MetricBase(MetricType::Counter, spec.name, spec.help) {
        core_->base_labels = spec.labels;
        core_->const_labels = spec.labels;
        core_->base_display = spec.display;
        core_->display = spec.display;
        if (!spec.unit.empty()) {
            core_->has_unit = true;
            core_->unit_from_dimval = spec.unit.from_dimval;
            core_->unit_name = std::string(spec.unit.name);
            core_->unit_kind = std::string(spec.unit.kind);
            core_->unit_symbol = std::string(spec.unit.symbol);
        }
    }

    /// Internal: adopt a core prepared by a `Registry` or `labels()`.
    explicit Counter(std::shared_ptr<MetricCore> core) : MetricBase(std::move(core)) {}

    /// Increment by one.
    void inc() const noexcept {
        record(NormalizedValue{1.0, Unit{}});
    }

    /// Increment by a raw arithmetic amount (must be >= 0 and finite).
    template <class T>
        requires std::is_arithmetic_v<T>
    void inc(T amount) noexcept {
        record(normalize(amount));
    }

    /// Increment by a dimensional amount; its unit is reconciled with the
    /// metric's declared/latched unit.
    template <DimensionalValue V>
    void inc(const V& amount) noexcept {
        record(normalize(amount));
    }

    /// A same-type child series bound to `dynamic` labels (overlaid on the
    /// family's constant labels by the backend).
    [[nodiscard]] Counter labels(const Labels& dynamic) const noexcept {
        return make_child(dynamic);
    }

private:
    void record(NormalizedValue nv) const noexcept {
        const auto [adapter, handle] = this->bind();
        if (!check_finite(nv.value, "inc")) {
            return;
        }
        if (nv.value < 0.0) {
            if (auto* lg = logger()) {
                lg->warn("counter '{}' dropping negative increment {}", core_->name, nv.value);
            }
            return;
        }
        if (!reconcile_unit(nv.unit, *adapter)) {
            return;
        }
        adapter->inc(handle, nv.value);
    }
};

}  // namespace prom
