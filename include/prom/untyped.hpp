#pragma once

/// @file
/// @brief `Untyped` — a bare value with no semantic constraints, and `UntypedSpec`.

#include <prom/metric_base.hpp>

#include <string_view>
#include <utility>

namespace prom {

/// Declarative description of an untyped metric.
struct UntypedSpec {
    std::string_view name{};
    std::string_view help{};
    Labels labels{};
    Unit unit{};
    comms::DisplayInfo display{};
};

/// An untyped metric: just a settable value. Useful for bridging foreign data
/// whose semantics prom should not second-guess.
class Untyped : public MetricBase<Untyped> {
public:
    Untyped(const std::string_view name, const std::string_view help)
        : MetricBase(MetricType::Untyped, name, help) {}

    explicit Untyped(const UntypedSpec& spec)
        : MetricBase(MetricType::Untyped, spec.name, spec.help) {
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

    explicit Untyped(std::shared_ptr<MetricCore> core) : MetricBase(std::move(core)) {}

    /// Set to a raw value.
    template <class T>
        requires std::is_arithmetic_v<T>
    void set(T value) noexcept {
        record(normalize(value));
    }

    /// Set to a dimensional value.
    template <DimensionalValue V>
    void set(const V& value) noexcept {
        record(normalize(value));
    }

    [[nodiscard]] Untyped labels(const Labels& dynamic) const noexcept {
        return make_child(dynamic);
    }

private:
    void record(const NormalizedValue& nv) const noexcept {
        const auto [adapter, handle] = this->bind();
        if (!check_finite(nv.value, "set")) {
            return;
        }
        if (!reconcile_unit(nv.unit, *adapter)) {
            return;
        }
        adapter->set(handle, nv.value);
    }
};

}  // namespace prom
