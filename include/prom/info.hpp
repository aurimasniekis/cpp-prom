#pragma once

/// @file
/// @brief `Info` — static key/value metadata exposed as an `*_info` series.

#include <prom/metric_base.hpp>

#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace prom {

/// Declarative description of an info metric.
struct InfoSpec {
    std::string_view name{};
    std::string_view help{};
    Labels labels{};  ///< Constant labels carried alongside the info set.
    comms::DisplayInfo display{};
};

/// An info metric: a single sample whose *labels* carry the payload (build
/// version, commit, ...). Backends typically render it as `name_info{...} 1`.
class Info : public MetricBase<Info> {
public:
    Info(const std::string_view name, const std::string_view help)
        : MetricBase(MetricType::Info, name, help) {}

    explicit Info(const InfoSpec& spec) : MetricBase(MetricType::Info, spec.name, spec.help) {
        core_->base_labels = spec.labels;
        core_->const_labels = spec.labels;
        core_->base_display = spec.display;
        core_->display = spec.display;
    }

    explicit Info(std::shared_ptr<MetricCore> core) : MetricBase(std::move(core)) {}

    /// Set the info label payload from a braced list.
    void set(const std::initializer_list<Label> labels) const noexcept {
        const std::vector<Label> owned(labels);
        emit(owned);
    }

    /// Set the info label payload from a `Labels` set.
    void set(const Labels& labels) const noexcept {
        const auto [adapter, handle] = this->bind();
        adapter->set_info(handle, labels.view());
    }

    [[nodiscard]] Info labels(const Labels& dynamic) const noexcept {
        return make_child(dynamic);
    }

private:
    void emit(const std::vector<Label>& labels) const noexcept {
        const auto [adapter, handle] = this->bind();
        adapter->set_info(handle, std::span<const Label>(labels));
    }
};

}  // namespace prom
