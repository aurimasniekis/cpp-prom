#pragma once

/// @file
/// @brief `Gauge` — a value that can move in both directions, and `GaugeSpec`.

#include <prom/metric_base.hpp>

#include <string_view>
#include <utility>

namespace prom {

/// Declarative description of a gauge.
struct GaugeSpec {
    std::string_view name{};
    std::string_view help{};
    Labels labels{};
    Unit unit{};
    comms::DisplayInfo display{};
};

/// A gauge. Supports `set`, `inc`, and `dec` with raw or dimensional values,
/// plus the no-argument `inc()`/`dec()` for +/-1.
class Gauge : public MetricBase<Gauge> {
public:
    Gauge(const std::string_view name, const std::string_view help)
        : MetricBase(MetricType::Gauge, name, help) {}

    explicit Gauge(const GaugeSpec& spec) : MetricBase(MetricType::Gauge, spec.name, spec.help) {
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

    explicit Gauge(std::shared_ptr<MetricCore> core) : MetricBase(std::move(core)) {}

    /// Set the gauge to a raw value.
    template <class T>
        requires std::is_arithmetic_v<T>
    void set(T value) noexcept {
        apply(&Adapter::set, normalize(value), "set");
    }

    /// Set the gauge to a dimensional value.
    template <DimensionalValue V>
    void set(const V& value) noexcept {
        apply(&Adapter::set, normalize(value), "set");
    }

    /// Increment by one.
    void inc() noexcept {
        apply(&Adapter::inc, NormalizedValue{1.0, Unit{}}, "inc");
    }

    /// Increment by a raw amount.
    template <class T>
        requires std::is_arithmetic_v<T>
    void inc(T amount) noexcept {
        apply(&Adapter::inc, normalize(amount), "inc");
    }

    /// Increment by a dimensional amount.
    template <DimensionalValue V>
    void inc(const V& amount) noexcept {
        apply(&Adapter::inc, normalize(amount), "inc");
    }

    /// Decrement by one.
    void dec() noexcept {
        apply(&Adapter::dec, NormalizedValue{1.0, Unit{}}, "dec");
    }

    /// Decrement by a raw amount.
    template <class T>
        requires std::is_arithmetic_v<T>
    void dec(T amount) noexcept {
        apply(&Adapter::dec, normalize(amount), "dec");
    }

    /// Decrement by a dimensional amount.
    template <DimensionalValue V>
    void dec(const V& amount) noexcept {
        apply(&Adapter::dec, normalize(amount), "dec");
    }

    [[nodiscard]] Gauge labels(const Labels& dynamic) const noexcept {
        return make_child(dynamic);
    }

private:
    using Op = void (Adapter::*)(const MetricHandle&, double) noexcept;

    void apply(const Op op, const NormalizedValue& nv, const std::string_view name) const noexcept {
        const auto [adapter, handle] = this->bind();
        if (!check_finite(nv.value, name)) {
            return;
        }
        if (!reconcile_unit(nv.unit, *adapter)) {
            return;
        }
        ((*adapter).*op)(handle, nv.value);
    }
};

}  // namespace prom
