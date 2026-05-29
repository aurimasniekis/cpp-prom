#pragma once

/// @file
/// @brief `Histogram` — bucketed observation distribution, and `HistogramSpec`.

#include <prom/metric_base.hpp>

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace prom {

/// The default bucket bounds applied when a histogram spec leaves `buckets`
/// empty — the canonical Prometheus client default set (seconds-oriented).
inline constexpr std::array<double, 11> default_histogram_buckets = {
    0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};

/// Declarative description of a histogram.
struct HistogramSpec {
    std::string_view name{};
    std::string_view help{};
    Labels labels{};
    Unit unit{};
    comms::DisplayInfo display{};
    std::vector<double> buckets{};  ///< Upper bounds; default set used when empty.
};

/// A histogram. Each `observe` lands in the cumulative bucket counts. Values
/// may be raw or dimensional.
class Histogram : public MetricBase<Histogram> {
public:
    Histogram(const std::string_view name, const std::string_view help)
        : MetricBase(MetricType::Histogram, name, help) {
        use_default_buckets();
    }

    explicit Histogram(const HistogramSpec& spec)
        : MetricBase(MetricType::Histogram, spec.name, spec.help) {
        core_->base_labels = spec.labels;
        core_->const_labels = spec.labels;
        core_->base_display = spec.display;
        core_->display = spec.display;
        core_->buckets = spec.buckets;
        if (core_->buckets.empty()) {
            use_default_buckets();
        }
        if (!spec.unit.empty()) {
            core_->has_unit = true;
            core_->unit_from_dimval = spec.unit.from_dimval;
            core_->unit_name = std::string(spec.unit.name);
            core_->unit_kind = std::string(spec.unit.kind);
            core_->unit_symbol = std::string(spec.unit.symbol);
        }
    }

    explicit Histogram(std::shared_ptr<MetricCore> core) : MetricBase(std::move(core)) {}

    /// Observe a raw value.
    template <class T>
        requires std::is_arithmetic_v<T>
    void observe(T value) noexcept {
        record(normalize(value));
    }

    /// Observe a dimensional value.
    template <DimensionalValue V>
    void observe(const V& value) noexcept {
        record(normalize(value));
    }

    [[nodiscard]] Histogram labels(const Labels& dynamic) const noexcept {
        return make_child(dynamic);
    }

private:
    void use_default_buckets() const {
        core_->buckets.assign(default_histogram_buckets.begin(), default_histogram_buckets.end());
    }

    void record(const NormalizedValue& nv) const noexcept {
        const auto [adapter, handle] = this->bind();
        if (!check_finite(nv.value, "observe")) {
            return;
        }
        if (!reconcile_unit(nv.unit, *adapter)) {
            return;
        }
        adapter->observe(handle, nv.value);
    }
};

}  // namespace prom
