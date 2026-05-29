#pragma once

/// @file
/// @brief `PrometheusCppAdapter` — a fully-functional prom backend built on
///        jupp0r/prometheus-cpp.
///
/// This is the header a host application includes to install a real backend:
///
/// @code
/// auto adapter = std::make_shared<prom::prometheus_cpp::PrometheusCppAdapter>();
/// prom::Registry::global()->set_adapter(adapter);
/// // ... expose adapter->registry() through an HTTP scrape endpoint ...
/// @endcode
///
/// The prometheus-cpp headers never appear here — they live entirely inside
/// `src/adapter.cpp` behind a pimpl, so prom's core stays free of them.

#include <prom/adapter.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace prometheus {
class Registry;
}

namespace prom::prometheus_cpp {

/// Maps prom's metric types onto prometheus-cpp families and series. Owns (or
/// shares) the underlying `prometheus::Registry`, which the host exposes via an
/// exposer/scrape endpoint.
///
/// **Mapping.** Counter/Gauge/Histogram/Summary map to their direct
/// prometheus-cpp equivalents. Untyped maps to a Gauge. Info maps to a
/// `<name>` Gauge whose label set carries the payload at value 1. StateSet maps
/// to a Gauge family with one series per state (labelled by the metric name),
/// each 0 or 1.
class PrometheusCppAdapter final : public prom::Adapter {
public:
    /// Construct with a freshly-allocated, owned registry.
    PrometheusCppAdapter();

    /// Construct around an externally-owned registry (shared), e.g. one already
    /// wired to an exposer.
    explicit PrometheusCppAdapter(std::shared_ptr<prometheus::Registry> registry);

    ~PrometheusCppAdapter() override;

    /// The registry backing this adapter — hand it to a `prometheus::Exposer`
    /// or serialize it for a scrape response.
    [[nodiscard]] prometheus::Registry& registry() const noexcept;

    /// Shared ownership of the backing registry.
    [[nodiscard]] std::shared_ptr<prometheus::Registry> registry_ptr() const noexcept;

    [[nodiscard]] std::string_view backend_name() const noexcept override;

    [[nodiscard]] prom::MetricHandle
    register_metric(const prom::MetricMeta& meta) noexcept override;
    [[nodiscard]] prom::MetricHandle resolve(const prom::MetricHandle& family,
                                             const prom::Labels& dynamic) noexcept override;

    void inc(const prom::MetricHandle& handle, double amount) noexcept override;
    void dec(const prom::MetricHandle& handle, double amount) noexcept override;
    void set(const prom::MetricHandle& handle, double value) noexcept override;
    void observe(const prom::MetricHandle& handle, double value) noexcept override;
    void set_info(const prom::MetricHandle& handle,
                  std::span<const prom::Label> labels) noexcept override;
    void set_state(const prom::MetricHandle& handle,
                   std::string_view state,
                   bool active) noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace prom::prometheus_cpp
