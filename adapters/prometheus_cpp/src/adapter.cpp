/// @file
/// @brief Implementation of `prom::prometheus_cpp::PrometheusCppAdapter`,
///        backed by jupp0r/prometheus-cpp. All prometheus-cpp headers are
///        confined to this translation unit.

#include <prom/prometheus_cpp/adapter.hpp>

#include <prometheus/counter.h>
#include <prometheus/family.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/labels.h>
#include <prometheus/registry.h>
#include <prometheus/summary.h>

#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prom::prometheus_cpp {

namespace {

/// Default quantile estimation error used when a SummarySpec gives only the
/// target quantiles (prometheus-cpp wants a (quantile, error) pair).
constexpr double default_quantile_error = 0.05;

[[nodiscard]] ::prometheus::Labels to_prom_labels(const prom::Labels& labels) {
    ::prometheus::Labels out;
    for (const auto& [name, value] : labels.view()) {
        out.emplace(name, value);
    }
    return out;
}

[[nodiscard]] ::prometheus::Labels to_prom_labels(std::span<const prom::Label> labels) {
    ::prometheus::Labels out;
    for (const auto& [name, value] : labels) {
        out.emplace(name, value);
    }
    return out;
}

/// Backend state for one prom metric: a family pointer plus, for the family's
/// default (unlabeled) series or a resolved child, the concrete series pointer.
/// Exactly the fields relevant to `type` are populated.
struct PromState : prom::MetricState {
    prom::MetricType type{};

    ::prometheus::Family<::prometheus::Counter>* counter_family = nullptr;
    ::prometheus::Family<::prometheus::Gauge>* gauge_family = nullptr;
    ::prometheus::Family<::prometheus::Histogram>* histogram_family = nullptr;
    ::prometheus::Family<::prometheus::Summary>* summary_family = nullptr;

    ::prometheus::Counter* counter = nullptr;
    ::prometheus::Gauge* gauge = nullptr;
    ::prometheus::Histogram* histogram = nullptr;
    ::prometheus::Summary* summary = nullptr;

    ::prometheus::Histogram::BucketBoundaries buckets;
    ::prometheus::Summary::Quantiles quantiles;

    std::string name;                            ///< Metric name (state-set label key).
    ::prometheus::Gauge* info_series = nullptr;  ///< Last info series, for replacement.
};

}  // namespace

class PrometheusCppAdapter::Impl {
public:
    explicit Impl(std::shared_ptr<::prometheus::Registry> registry)
        : registry_(std::move(registry)) {}

    std::shared_ptr<::prometheus::Registry> registry_;
    std::mutex mutex_;  ///< Guards family creation/resolution.
};

PrometheusCppAdapter::PrometheusCppAdapter()
    : impl_(std::make_unique<Impl>(std::make_shared<::prometheus::Registry>())) {}

PrometheusCppAdapter::PrometheusCppAdapter(std::shared_ptr<::prometheus::Registry> registry)
    : impl_(std::make_unique<Impl>(std::move(registry))) {}

PrometheusCppAdapter::~PrometheusCppAdapter() = default;

::prometheus::Registry& PrometheusCppAdapter::registry() const noexcept {
    return *impl_->registry_;
}

std::shared_ptr<::prometheus::Registry> PrometheusCppAdapter::registry_ptr() const noexcept {
    return impl_->registry_;
}

std::string_view PrometheusCppAdapter::backend_name() const noexcept {
    return "prometheus-cpp";
}

prom::MetricHandle PrometheusCppAdapter::register_metric(const prom::MetricMeta& meta) noexcept {
    try {
        const std::lock_guard lock(impl_->mutex_);
        auto state = std::make_shared<PromState>();
        state->type = meta.type;
        state->name = std::string(meta.name);

        const std::string name(meta.name);
        const std::string help(meta.help);
        const auto labels = to_prom_labels(meta.const_labels);
        auto& reg = *impl_->registry_;

        switch (meta.type) {
        case prom::MetricType::Counter: {
            auto& family =
                ::prometheus::BuildCounter().Name(name).Help(help).Labels(labels).Register(reg);
            state->counter_family = &family;
            state->counter = &family.Add({});
            break;
        }
        case prom::MetricType::Gauge:
        case prom::MetricType::Untyped: {
            auto& family =
                ::prometheus::BuildGauge().Name(name).Help(help).Labels(labels).Register(reg);
            state->gauge_family = &family;
            state->gauge = &family.Add({});
            break;
        }
        case prom::MetricType::Histogram: {
            auto& family =
                ::prometheus::BuildHistogram().Name(name).Help(help).Labels(labels).Register(reg);
            state->histogram_family = &family;
            state->buckets.assign(meta.buckets.begin(), meta.buckets.end());
            state->histogram = &family.Add({}, state->buckets);
            break;
        }
        case prom::MetricType::Summary: {
            auto& family =
                ::prometheus::BuildSummary().Name(name).Help(help).Labels(labels).Register(reg);
            state->summary_family = &family;
            for (double q : meta.quantiles) {
                state->quantiles.emplace_back(q, default_quantile_error);
            }
            state->summary = &family.Add({}, state->quantiles);
            break;
        }
        case prom::MetricType::Info:
        case prom::MetricType::StateSet: {
            // Both are realized as gauge families; their series are created
            // lazily in set_info / set_state.
            auto& family =
                ::prometheus::BuildGauge().Name(name).Help(help).Labels(labels).Register(reg);
            state->gauge_family = &family;
            break;
        }
        }
        return state;
    } catch (...) {
        // Honor the noexcept, non-null contract: hand back an inert handle.
        return std::make_shared<prom::MetricState>();
    }
}

prom::MetricHandle PrometheusCppAdapter::resolve(const prom::MetricHandle& family,
                                                 const prom::Labels& dynamic) noexcept {
    try {
        const std::lock_guard lock(impl_->mutex_);
        const auto* parent = dynamic_cast<PromState*>(family.get());
        if (parent == nullptr) {
            return family;
        }
        const auto labels = to_prom_labels(dynamic);

        auto child = std::make_shared<PromState>();
        child->type = parent->type;
        child->name = parent->name;

        switch (parent->type) {
        case prom::MetricType::Counter:
            child->counter_family = parent->counter_family;
            child->counter = &parent->counter_family->Add(labels);
            break;
        case prom::MetricType::Gauge:
        case prom::MetricType::Untyped:
        case prom::MetricType::Info:
        case prom::MetricType::StateSet:
            child->gauge_family = parent->gauge_family;
            child->gauge = &parent->gauge_family->Add(labels);
            break;
        case prom::MetricType::Histogram:
            child->histogram_family = parent->histogram_family;
            child->buckets = parent->buckets;
            child->histogram = &parent->histogram_family->Add(labels, child->buckets);
            break;
        case prom::MetricType::Summary:
            child->summary_family = parent->summary_family;
            child->quantiles = parent->quantiles;
            child->summary = &parent->summary_family->Add(labels, child->quantiles);
            break;
        }
        return child;
    } catch (...) {
        return family;
    }
}

void PrometheusCppAdapter::inc(const prom::MetricHandle& handle, const double amount) noexcept {
    const auto* s = dynamic_cast<PromState*>(handle.get());
    if (s == nullptr) {
        return;
    }
    if (s->counter != nullptr) {
        s->counter->Increment(amount);
    } else if (s->gauge != nullptr) {
        s->gauge->Increment(amount);
    }
}

void PrometheusCppAdapter::dec(const prom::MetricHandle& handle, const double amount) noexcept {
    if (const auto* s = dynamic_cast<PromState*>(handle.get());
        s != nullptr && s->gauge != nullptr) {
        s->gauge->Decrement(amount);
    }
}

void PrometheusCppAdapter::set(const prom::MetricHandle& handle, const double value) noexcept {
    if (const auto* s = dynamic_cast<PromState*>(handle.get());
        s != nullptr && s->gauge != nullptr) {
        s->gauge->Set(value);
    }
}

void PrometheusCppAdapter::observe(const prom::MetricHandle& handle, const double value) noexcept {
    const auto* s = dynamic_cast<PromState*>(handle.get());
    if (s == nullptr) {
        return;
    }
    if (s->histogram != nullptr) {
        s->histogram->Observe(value);
    } else if (s->summary != nullptr) {
        s->summary->Observe(value);
    }
}

void PrometheusCppAdapter::set_info(const prom::MetricHandle& handle,
                                    const std::span<const prom::Label> labels) noexcept {
    try {
        const std::lock_guard lock(impl_->mutex_);
        auto* s = dynamic_cast<PromState*>(handle.get());
        if (s == nullptr || s->gauge_family == nullptr) {
            return;
        }
        // Replace any previous info series so stale label sets don't linger.
        if (s->info_series != nullptr) {
            s->gauge_family->Remove(s->info_series);
        }
        s->info_series = &s->gauge_family->Add(to_prom_labels(labels));
        s->info_series->Set(1.0);
    } catch (...) {
        // best-effort
    }
}

void PrometheusCppAdapter::set_state(const prom::MetricHandle& handle,
                                     const std::string_view state,
                                     const bool active) noexcept {
    try {
        const std::lock_guard lock(impl_->mutex_);
        auto* s = dynamic_cast<PromState*>(handle.get());
        if (s == nullptr || s->gauge_family == nullptr) {
            return;
        }
        // OpenMetrics state sets expose one series per state, labelled by the
        // metric name, each carrying 0 or 1.
        ::prometheus::Labels labels;
        labels.emplace(s->name, std::string(state));
        auto& series = s->gauge_family->Add(labels);
        series.Set(active ? 1.0 : 0.0);
    } catch (...) {
        // best-effort
    }
}

}  // namespace prom::prometheus_cpp
