#pragma once

/// @file
/// @brief `Registry` — the front door for creating *registered* metrics.
///
/// A `Registry` owns an `AdapterCell`: the adapter lives on the registry, not on
/// the metrics it creates, so swapping the registry's adapter reconfigures every
/// metric created from it (each re-registers against the new backend on its next
/// use). Each factory validates its spec, builds a stable `MetricCore` pointing
/// at the registry's cell, eagerly registers the family, tracks it for
/// enumeration, and returns a typed metric. The throwing factories (`counter`,
/// ...) raise `prom::Exception`; the `try_*` mirrors are `noexcept` and return
/// `prom::expected`.
///
/// `Registry` is non-copyable and `shared_ptr`-managed: obtain one with
/// `Registry::create(adapter)` or the process-wide `Registry::global()`, and use
/// it through `->`.
///
/// The `*Spec` structs live with their metric types (`<prom/counter.hpp>` …)
/// and are all visible through this header.

#include <prom/adapter.hpp>
#include <prom/counter.hpp>
#include <prom/error.hpp>
#include <prom/gauge.hpp>
#include <prom/global.hpp>
#include <prom/histogram.hpp>
#include <prom/info.hpp>
#include <prom/labels.hpp>
#include <prom/metric_base.hpp>
#include <prom/stateset.hpp>
#include <prom/summary.hpp>
#include <prom/unit.hpp>
#include <prom/untyped.hpp>

#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prom {

class Scope;
struct ScopeConfig;

/// The decoration a `Registry` applies to every metric created through it: a
/// name prefix, default constant labels, and default display metadata. Supplied
/// to `Registry::create(adapter, config)` and live-reconfigurable afterward
/// through the registry's setters, exactly like a `Scope`. A registry created
/// without one starts with an empty decoration (its metrics report
/// `scoped == false` until a setter installs a prefix or labels).
struct RegistryConfig {
    /// Prepended to every metric name (e.g. `"svc_"`).
    std::string prefix{};
    /// Merged into every metric's constant labels; a metric's own labels win on
    /// a name collision.
    Labels const_labels{};
    /// Default display metadata; per-metric display fields override these.
    comms::DisplayInfo display{};
};

/// A read-only snapshot describing one registered metric, returned by the
/// enumeration APIs (`Registry::metrics`, `Scope::metrics`). For a scoped metric
/// the `name` and `const_labels` are the *effective* (scope-decorated) values
/// computed live. The `unit`'s string views reference storage owned by the live
/// metric, so a `MetricInfo` is valid only while that metric is alive.
struct MetricInfo {
    MetricType type{};
    std::string name;
    std::string help;
    Labels const_labels;
    Unit unit;
    bool scoped = false;
};

/// Build a `MetricInfo` from a core, applying scope decoration for a scoped
/// metric's effective name and constant labels.
[[nodiscard]] inline MetricInfo describe(const MetricCore& core) {
    MetricInfo info;
    info.type = core.type;
    if (core.scope) {
        info.name = core.scope->full_name(core.base_name);
        info.const_labels = core.scope->effective_labels(core.base_labels);
    } else {
        info.name = core.name;
        info.const_labels = core.const_labels;
    }
    info.help = core.help;
    info.unit = core.unit_view();
    // "scoped" means the metric is actually decorated: an empty decoration (a
    // plain registry, or an unconfigured global) leaves it reported as plain.
    info.scoped = core.scope && core.scope->decorates();
    return info;
}

/// Creates registered metrics against a single adapter cell.
///
/// The adapter lives on the registry's `AdapterCell`. `set_adapter` swaps it and
/// every metric created from the registry re-registers against the new backend
/// on its next use (the previous backend's series are left behind). The
/// process-wide `Registry::global()` shares the global cell — the same one
/// standalone and scoped metrics read — so installing a backend with
/// `Registry::global()->set_adapter(...)` reconfigures everything at once.
///
/// Non-copyable and `shared_ptr`-managed: use `Registry::create(...)` /
/// `Registry::global()` and call through `->`.
class Registry {
public:
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(Registry&&) = delete;
    ~Registry() = default;

    /// Create a registry with its own cell holding `adapter` (a fresh
    /// `NullAdapter` when null).
    [[nodiscard]] static std::shared_ptr<Registry> create(AdapterPtr adapter = nullptr) {
        return std::shared_ptr<Registry>(
            new Registry(std::make_shared<AdapterCell>(std::move(adapter))));
    }

    /// Create a *decorating* registry: every metric built from it gets `config`'s
    /// prefix, default constant labels, and default display, all live-
    /// reconfigurable through the registry's setters (mutating them re-registers
    /// existing metrics on next use, just like a `Scope`).
    [[nodiscard]] static std::shared_ptr<Registry> create(AdapterPtr adapter,
                                                          RegistryConfig config) {
        return std::shared_ptr<Registry>(
            new Registry(std::make_shared<AdapterCell>(std::move(adapter)),
                         std::make_shared<detail::DecorationState>(std::move(config.prefix),
                                                                   std::move(config.const_labels),
                                                                   std::move(config.display))));
    }

    /// The process-wide registry, sharing the global adapter cell *and* the
    /// global decoration. Lets callers create metrics without threading a
    /// `Registry` through their code — see the free `prom::counter(...)` helpers,
    /// which delegate here. Install a backend with
    /// `prom::Registry::global()->set_adapter(...)`; install a process-wide
    /// prefix / labels with `prom::Registry::global()->set_prefix(...)` (which
    /// reaches standalone and scoped metrics too).
    [[nodiscard]] static std::shared_ptr<Registry> global() {
        static std::shared_ptr<Registry> instance(
            new Registry(detail::global_adapter_cell(), detail::global_decoration()));
        return instance;
    }

    /// The adapter this registry binds metrics to.
    [[nodiscard]] AdapterPtr adapter() const {
        return cell_->adapter();
    }

    /// Install `adapter` (or reset to a fresh `NullAdapter` when null) as this
    /// registry's backend. Metrics already created from the registry migrate to
    /// it on their next use; series already written to the previous backend stay
    /// there (backends cannot move a registered series).
    void set_adapter(AdapterPtr adapter) const {
        cell_->set_adapter(std::move(adapter));
    }

    // --- live decoration (prefix / default labels / display) ----------------
    //
    // Every registry carries a decoration (empty by default), so these setters
    // reconfigure *all* metrics it has created — existing and future — which
    // re-register on their next use. On `Registry::global()` the decoration is
    // the process-wide one, so a prefix / label set here also reaches standalone
    // and scoped metrics.

    [[nodiscard]] std::string prefix() const {
        return decoration_->prefix();
    }
    void set_prefix(std::string prefix) {
        decoration_->set_prefix(std::move(prefix));
    }

    [[nodiscard]] Labels const_labels() const {
        return decoration_->const_labels();
    }
    void set_const_labels(Labels labels) {
        decoration_->set_const_labels(std::move(labels));
    }
    void add_const_label(std::string name, std::string value) {
        decoration_->add_const_label(std::move(name), std::move(value));
    }

    [[nodiscard]] comms::DisplayInfo display() const {
        return decoration_->display();
    }
    void set_display(comms::DisplayInfo display) {
        decoration_->set_display(std::move(display));
    }

    /// A snapshot of the whole decoration.
    [[nodiscard]] RegistryConfig config() const {
        return RegistryConfig{
            decoration_->prefix(), decoration_->const_labels(), decoration_->display()};
    }
    /// Replace the whole decoration at once (single reconfiguration).
    void configure(RegistryConfig config) {
        decoration_->configure(
            std::move(config.prefix), std::move(config.const_labels), std::move(config.display));
    }

    /// Snapshots describing every (still-alive) metric created from this
    /// registry, including declared-but-unused ones. Expired entries are pruned.
    [[nodiscard]] std::vector<MetricInfo> metrics() const {
        // Collect live cores under the lock, then describe() after releasing it
        // (keeps the lock hold short and avoids any reentrancy on describe()).
        std::vector<std::shared_ptr<MetricCore>> live;
        {
            const std::scoped_lock lock(mutex_);
            for (auto it = metrics_.begin(); it != metrics_.end();) {
                if (auto core = it->lock()) {
                    live.push_back(std::move(core));
                    ++it;
                } else {
                    it = metrics_.erase(it);
                }
            }
        }
        std::vector<MetricInfo> out;
        out.reserve(live.size());
        for (const auto& core : live) {
            out.push_back(describe(*core));
        }
        return out;
    }

    /// Get-or-create the process-wide, named `Scope` (a per-library metrics
    /// instance with a shared prefix / default labels / display). Defined in
    /// `<prom/scope.hpp>` — include it to use these. The two-argument form's
    /// config applies only when the scope is first created.
    [[nodiscard]] static std::shared_ptr<Scope> scope(std::string_view name);
    [[nodiscard]] static std::shared_ptr<Scope> scope(std::string_view name, ScopeConfig config);

    // --- throwing factories -------------------------------------------------

    [[nodiscard]] Counter counter(const CounterSpec& spec) {
        return unwrap(try_counter(spec));
    }
    [[nodiscard]] Gauge gauge(const GaugeSpec& spec) {
        return unwrap(try_gauge(spec));
    }
    [[nodiscard]] Histogram histogram(const HistogramSpec& spec) {
        return unwrap(try_histogram(spec));
    }
    [[nodiscard]] Summary summary(const SummarySpec& spec) {
        return unwrap(try_summary(spec));
    }
    [[nodiscard]] Untyped untyped(const UntypedSpec& spec) {
        return unwrap(try_untyped(spec));
    }
    [[nodiscard]] Info info(const InfoSpec& spec) {
        return unwrap(try_info(spec));
    }
    [[nodiscard]] StateSet stateset(const StateSetSpec& spec) {
        return unwrap(try_stateset(spec));
    }

    // --- noexcept fallible mirrors -----------------------------------------

    [[nodiscard]] expected<Counter> try_counter(const CounterSpec& spec) noexcept {
        return build<Counter>(MetricType::Counter,
                              spec.name,
                              spec.help,
                              spec.labels,
                              spec.unit,
                              spec.display,
                              {},
                              {},
                              {});
    }

    [[nodiscard]] expected<Gauge> try_gauge(const GaugeSpec& spec) noexcept {
        return build<Gauge>(MetricType::Gauge,
                            spec.name,
                            spec.help,
                            spec.labels,
                            spec.unit,
                            spec.display,
                            {},
                            {},
                            {});
    }

    [[nodiscard]] expected<Histogram> try_histogram(const HistogramSpec& spec) noexcept {
        std::vector<double> buckets = spec.buckets;
        if (buckets.empty()) {
            buckets.assign(default_histogram_buckets.begin(), default_histogram_buckets.end());
        }
        if (const auto err = validate_buckets(buckets)) {
            return std::unexpected(*err);
        }
        return build<Histogram>(MetricType::Histogram,
                                spec.name,
                                spec.help,
                                spec.labels,
                                spec.unit,
                                spec.display,
                                std::move(buckets),
                                {},
                                {});
    }

    [[nodiscard]] expected<Summary> try_summary(const SummarySpec& spec) noexcept {
        std::vector<double> quantiles = spec.quantiles;
        if (quantiles.empty()) {
            quantiles.assign(default_summary_quantiles.begin(), default_summary_quantiles.end());
        }
        if (const auto err = validate_quantiles(quantiles)) {
            return std::unexpected(*err);
        }
        return build<Summary>(MetricType::Summary,
                              spec.name,
                              spec.help,
                              spec.labels,
                              spec.unit,
                              spec.display,
                              {},
                              std::move(quantiles),
                              {});
    }

    [[nodiscard]] expected<Untyped> try_untyped(const UntypedSpec& spec) noexcept {
        return build<Untyped>(MetricType::Untyped,
                              spec.name,
                              spec.help,
                              spec.labels,
                              spec.unit,
                              spec.display,
                              {},
                              {},
                              {});
    }

    [[nodiscard]] expected<Info> try_info(const InfoSpec& spec) noexcept {
        return build<Info>(
            MetricType::Info, spec.name, spec.help, spec.labels, Unit{}, spec.display, {}, {}, {});
    }

    [[nodiscard]] expected<StateSet> try_stateset(const StateSetSpec& spec) noexcept {
        if (spec.states.empty()) {
            return std::unexpected(Error{ErrorCode::EmptyStateSet, std::string(spec.name)});
        }
        return build<StateSet>(MetricType::StateSet,
                               spec.name,
                               spec.help,
                               spec.labels,
                               Unit{},
                               spec.display,
                               {},
                               {},
                               spec.states);
    }

private:
    template <class Metric>
    [[nodiscard]] expected<Metric> build(const MetricType type,
                                         const std::string_view name,
                                         const std::string_view help,
                                         const Labels& labels,
                                         const Unit& unit,
                                         const comms::DisplayInfo& display,
                                         std::vector<double> buckets,
                                         std::vector<double> quantiles,
                                         std::vector<std::string> states) noexcept {
        if (!is_valid_metric_name(name)) {
            return std::unexpected(Error{ErrorCode::InvalidMetricName, std::string(name)});
        }
        for (const auto& [label_name, value] : labels.view()) {
            if (!is_valid_label_name(label_name)) {
                return std::unexpected(Error{ErrorCode::InvalidLabelName, label_name});
            }
        }

        auto core = std::make_shared<MetricCore>();
        core->type = type;
        core->help = std::string(help);
        core->buckets = std::move(buckets);
        core->quantiles = std::move(quantiles);
        core->states = std::move(states);
        core->source = cell_;
        if (!unit.empty()) {
            core->has_unit = true;
            core->unit_from_dimval = unit.from_dimval;
            core->unit_name = std::string(unit.name);
            core->unit_kind = std::string(unit.kind);
            core->unit_symbol = std::string(unit.symbol);
        }

        // Resolve the effective name / labels / display from the registry's live
        // decoration, keeping the base values so the metric can re-resolve when
        // the decoration changes (same machinery as a Scope). The decoration is
        // empty by default, in which case the effective values equal the spec's.
        core->scope = decoration_;
        core->base_name = std::string(name);
        core->base_labels = labels;
        core->base_display = display;
        const std::uint64_t decoration_version = decoration_->version();
        core->name = decoration_->full_name(name);
        core->const_labels = decoration_->effective_labels(labels);
        core->display = decoration_->effective_display(display);

        // Register eagerly against the current adapter so the family exists at
        // creation and `metrics()` can list it before first use. A later
        // adapter swap bumps the cell version, and a decoration change bumps the
        // scope version; either triggers re-registration on next use.
        const std::uint64_t adapter_version = cell_->version();
        const AdapterPtr adapter = cell_->adapter();
        const MetricHandle handle = adapter->register_metric(core->build_meta());
        core->bound.store(std::make_shared<const MetricCore::Bound>(
            MetricCore::Bound{.adapter = adapter,
                              .handle = handle,
                              .adapter_version = adapter_version,
                              .scope_version = decoration_version}));
        track(core);
        return Metric{std::move(core)};
    }

    /// Remember `core` (weakly) so `metrics()` can enumerate it.
    void track(const std::shared_ptr<MetricCore>& core) const noexcept {
        const std::scoped_lock lock(mutex_);
        metrics_.push_back(core);
    }

    template <class Metric>
    [[nodiscard]] static Metric unwrap(expected<Metric> result) {
        if (!result) {
            throw Exception(std::move(result.error()));
        }
        return std::move(*result);
    }

    [[nodiscard]] static std::optional<Error>
    validate_buckets(const std::vector<double>& buckets) noexcept {
        if (buckets.empty()) {
            return Error{ErrorCode::InvalidBuckets, "no buckets"};
        }
        for (std::size_t i = 0; i < buckets.size(); ++i) {
            if (!std::isfinite(buckets[i])) {
                return Error{ErrorCode::InvalidBuckets, "non-finite bound"};
            }
            if (i > 0 && buckets[i] <= buckets[i - 1]) {
                return Error{ErrorCode::InvalidBuckets, "bounds not strictly increasing"};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<Error>
    validate_quantiles(const std::vector<double>& quantiles) noexcept {
        for (const double q : quantiles) {
            if (!std::isfinite(q) || q <= 0.0 || q >= 1.0) {
                return Error{ErrorCode::InvalidQuantiles, "quantile outside (0, 1)"};
            }
        }
        return std::nullopt;
    }

    /// A registry with its own fresh (empty) decoration.
    explicit Registry(std::shared_ptr<AdapterCell> cell) noexcept
        : Registry(std::move(cell), std::make_shared<detail::DecorationState>()) {}

    /// A registry sharing a specific decoration — used by `global()` to adopt
    /// the process-wide one and by `create(adapter, config)`.
    Registry(std::shared_ptr<AdapterCell> cell,
             std::shared_ptr<detail::DecorationState> decoration) noexcept
        : cell_(std::move(cell)), decoration_(std::move(decoration)) {}

    std::shared_ptr<AdapterCell> cell_;
    mutable std::mutex mutex_;
    mutable std::vector<std::weak_ptr<MetricCore>> metrics_;
    // The live prefix / labels / display applied to every metric this registry
    // creates. Set once at construction (never null), so reads need no lock.
    std::shared_ptr<detail::DecorationState> decoration_;
};

// --- free helpers bound to Registry::global() -------------------------------
//
// These let a library create metrics without holding or passing a Registry:
//   auto c = prom::counter({.name = "requests_total", .help = "..."});
// Each delegates to the process-wide Registry::global(), which registers
// against whatever adapter is installed on the global cell at the time of the
// call (a NullAdapter until a backend is installed).

[[nodiscard]] inline Counter counter(const CounterSpec& spec) {
    return Registry::global()->counter(spec);
}
[[nodiscard]] inline Gauge gauge(const GaugeSpec& spec) {
    return Registry::global()->gauge(spec);
}
[[nodiscard]] inline Histogram histogram(const HistogramSpec& spec) {
    return Registry::global()->histogram(spec);
}
[[nodiscard]] inline Summary summary(const SummarySpec& spec) {
    return Registry::global()->summary(spec);
}
[[nodiscard]] inline Untyped untyped(const UntypedSpec& spec) {
    return Registry::global()->untyped(spec);
}
[[nodiscard]] inline Info info(const InfoSpec& spec) {
    return Registry::global()->info(spec);
}
[[nodiscard]] inline StateSet stateset(const StateSetSpec& spec) {
    return Registry::global()->stateset(spec);
}

[[nodiscard]] inline expected<Counter> try_counter(const CounterSpec& spec) noexcept {
    return Registry::global()->try_counter(spec);
}
[[nodiscard]] inline expected<Gauge> try_gauge(const GaugeSpec& spec) noexcept {
    return Registry::global()->try_gauge(spec);
}
[[nodiscard]] inline expected<Histogram> try_histogram(const HistogramSpec& spec) noexcept {
    return Registry::global()->try_histogram(spec);
}
[[nodiscard]] inline expected<Summary> try_summary(const SummarySpec& spec) noexcept {
    return Registry::global()->try_summary(spec);
}
[[nodiscard]] inline expected<Untyped> try_untyped(const UntypedSpec& spec) noexcept {
    return Registry::global()->try_untyped(spec);
}
[[nodiscard]] inline expected<Info> try_info(const InfoSpec& spec) noexcept {
    return Registry::global()->try_info(spec);
}
[[nodiscard]] inline expected<StateSet> try_stateset(const StateSetSpec& spec) noexcept {
    return Registry::global()->try_stateset(spec);
}

}  // namespace prom
