#pragma once

/// @file
/// @brief `Summary` — streaming quantile summary, and `SummarySpec`.

#include <prom/metric_base.hpp>

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace prom {

/// Default quantiles applied when a summary spec leaves `quantiles` empty.
inline constexpr std::array<double, 3> default_summary_quantiles = {0.5, 0.9, 0.99};

/// Declarative description of a summary.
struct SummarySpec {
    std::string_view name{};
    std::string_view help{};
    Labels labels{};
    Unit unit{};
    comms::DisplayInfo display{};
    std::vector<double> quantiles{};  ///< Target quantiles; default set used when empty.
};

/// A summary. Like a histogram but tracks quantiles instead of fixed buckets.
class Summary : public MetricBase<Summary> {
public:
    Summary(const std::string_view name, const std::string_view help)
        : MetricBase(MetricType::Summary, name, help) {
        use_default_quantiles();
    }

    explicit Summary(const SummarySpec& spec)
        : MetricBase(MetricType::Summary, spec.name, spec.help) {
        core_->base_labels = spec.labels;
        core_->const_labels = spec.labels;
        core_->base_display = spec.display;
        core_->display = spec.display;
        core_->quantiles = spec.quantiles;
        if (core_->quantiles.empty()) {
            use_default_quantiles();
        }
        if (!spec.unit.empty()) {
            core_->has_unit = true;
            core_->unit_from_dimval = spec.unit.from_dimval;
            core_->unit_name = std::string(spec.unit.name);
            core_->unit_kind = std::string(spec.unit.kind);
            core_->unit_symbol = std::string(spec.unit.symbol);
        }
    }

    explicit Summary(std::shared_ptr<MetricCore> core) : MetricBase(std::move(core)) {}

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

    [[nodiscard]] Summary labels(const Labels& dynamic) const noexcept {
        return make_child(dynamic);
    }

private:
    void use_default_quantiles() const {
        core_->quantiles.assign(default_summary_quantiles.begin(), default_summary_quantiles.end());
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
