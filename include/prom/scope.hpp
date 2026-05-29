#pragma once

/// @file
/// @brief `Scope` — a per-library metrics instance with a shared name prefix,
///        default constant labels, and default display metadata.
///
/// A `Scope` is to prom what a named logger is to a logging library: a library
/// configures one instance for itself (`prom::scope("foo", {.prefix = "foo_",
/// ...})`), stores it, and creates all its metrics through it. Scopes are
/// registered process-wide by name, so a *user* of the library can fetch the
/// same scope (`prom::scope("foo")`) and adjust it.
///
/// **The scope config is live, not cached.** A scoped metric does not copy the
/// prefix / labels / display at creation. It re-resolves them whenever the
/// scope's configuration changes, so mutating a scope at runtime reconfigures
/// every metric created from it — subsequent samples flow to the newly-derived
/// series. (Already-emitted series under the old name/labels are left as-is;
/// backends cannot rename a registered series.)
///
/// Scopes are deliberately **adapter-agnostic**: they only decorate specs and
/// hand them to `Registry::global()`. All adapter resolution stays in the
/// registry layer.
///
/// A scope's decoration **chains onto the process-wide global decoration** (the
/// one `Registry::global()->set_prefix(...)` mutates), applied outermost: a
/// global prefix `reg_` under a scope prefix `foo_` yields `reg_foo_meter`, and
/// label precedence is own → scope → global.

#include <prom/metric_base.hpp>
#include <prom/registry.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace prom {

/// The configuration a `Scope` applies to every metric created through it.
struct ScopeConfig {
    /// Prepended to every metric name (e.g. `"foo_"`).
    std::string prefix{};
    /// Merged into every metric's constant labels; a metric's own labels win
    /// on a name collision.
    Labels const_labels{};
    /// Default display metadata; per-metric display fields override these.
    comms::DisplayInfo display{};
};

/// A named, reconfigurable metrics instance for one library. Create/retrieve it
/// with `prom::scope(...)` (or `prom::Registry::scope(...)`); it is held by
/// `shared_ptr` so the metrics created from it keep it alive.
class Scope : public ScopeState, public std::enable_shared_from_this<Scope> {
public:
    explicit Scope(ScopeConfig config = {})
        : deco_(std::move(config.prefix),
                std::move(config.const_labels),
                std::move(config.display),
                detail::global_decoration()) {}

    // --- metric factories (names get the prefix; labels/display get merged) --

    [[nodiscard]] Counter counter(const CounterSpec& spec) {
        return make<Counter>(MetricType::Counter,
                             spec.name,
                             spec.help,
                             spec.labels,
                             spec.unit,
                             spec.display,
                             {},
                             {},
                             {});
    }
    [[nodiscard]] Gauge gauge(const GaugeSpec& spec) {
        return make<Gauge>(MetricType::Gauge,
                           spec.name,
                           spec.help,
                           spec.labels,
                           spec.unit,
                           spec.display,
                           {},
                           {},
                           {});
    }
    [[nodiscard]] Histogram histogram(const HistogramSpec& spec) {
        std::vector<double> buckets = spec.buckets;
        if (buckets.empty()) {
            buckets.assign(default_histogram_buckets.begin(), default_histogram_buckets.end());
        }
        return make<Histogram>(MetricType::Histogram,
                               spec.name,
                               spec.help,
                               spec.labels,
                               spec.unit,
                               spec.display,
                               std::move(buckets),
                               {},
                               {});
    }
    [[nodiscard]] Summary summary(const SummarySpec& spec) {
        std::vector<double> quantiles = spec.quantiles;
        if (quantiles.empty()) {
            quantiles.assign(default_summary_quantiles.begin(), default_summary_quantiles.end());
        }
        return make<Summary>(MetricType::Summary,
                             spec.name,
                             spec.help,
                             spec.labels,
                             spec.unit,
                             spec.display,
                             {},
                             std::move(quantiles),
                             {});
    }
    [[nodiscard]] Untyped untyped(const UntypedSpec& spec) {
        return make<Untyped>(MetricType::Untyped,
                             spec.name,
                             spec.help,
                             spec.labels,
                             spec.unit,
                             spec.display,
                             {},
                             {},
                             {});
    }
    [[nodiscard]] Info info(const InfoSpec& spec) {
        return make<Info>(
            MetricType::Info, spec.name, spec.help, spec.labels, Unit{}, spec.display, {}, {}, {});
    }
    [[nodiscard]] StateSet stateset(const StateSetSpec& spec) {
        return make<StateSet>(MetricType::StateSet,
                              spec.name,
                              spec.help,
                              spec.labels,
                              Unit{},
                              spec.display,
                              {},
                              {},
                              spec.states);
    }

    // --- live configuration (mutations reconfigure existing scoped metrics) --

    [[nodiscard]] std::string prefix() const {
        return deco_.prefix();
    }
    void set_prefix(std::string prefix) {
        deco_.set_prefix(std::move(prefix));
    }

    [[nodiscard]] Labels const_labels() const {
        return deco_.const_labels();
    }
    void set_const_labels(Labels labels) {
        deco_.set_const_labels(std::move(labels));
    }
    void add_const_label(std::string name, std::string value) {
        deco_.add_const_label(std::move(name), std::move(value));
    }

    [[nodiscard]] comms::DisplayInfo display() const {
        return deco_.display();
    }
    void set_display(comms::DisplayInfo display) {
        deco_.set_display(std::move(display));
    }

    /// A snapshot of the whole configuration.
    [[nodiscard]] ScopeConfig config() const {
        return ScopeConfig{deco_.prefix(), deco_.const_labels(), deco_.display()};
    }
    /// Replace the whole configuration at once.
    void configure(ScopeConfig config) {
        deco_.configure(
            std::move(config.prefix), std::move(config.const_labels), std::move(config.display));
    }

    // --- ScopeState (consumed by scoped metrics on each use) ----------------

    [[nodiscard]] std::uint64_t version() const noexcept override {
        return deco_.version();
    }
    [[nodiscard]] std::string full_name(const std::string_view base) const override {
        return deco_.full_name(base);
    }
    [[nodiscard]] Labels effective_labels(const Labels& own) const override {
        return deco_.effective_labels(own);
    }
    [[nodiscard]] comms::DisplayInfo
    effective_display(const comms::DisplayInfo& own) const override {
        return deco_.effective_display(own);
    }
    [[nodiscard]] bool decorates() const noexcept override {
        return deco_.decorates();
    }

    // --- enumeration --------------------------------------------------------

    /// Snapshots describing every (still-alive) metric created through this
    /// scope, including declared-but-unused ones; the effective name and labels
    /// are computed live from the current scope config. Expired entries are
    /// pruned. Kept separate from `Registry::global()->metrics()`.
    [[nodiscard]] std::vector<MetricInfo> metrics() const {
        // Collect live cores under the lock, then describe() *after* releasing
        // it, keeping the lock hold short.
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

private:
    template <class Metric>
    [[nodiscard]] Metric make(const MetricType type,
                              const std::string_view name,
                              const std::string_view help,
                              const Labels& labels,
                              const Unit& unit,
                              const comms::DisplayInfo& display,
                              std::vector<double> buckets,
                              std::vector<double> quantiles,
                              std::vector<std::string> states) {
        auto core = std::make_shared<MetricCore>();
        core->type = type;
        core->scope = shared_from_this();              // keeps this Scope alive
        core->source = detail::global_adapter_cell();  // scopes follow the global adapter
        core->base_name = std::string(name);
        core->help = std::string(help);
        core->base_labels = labels;
        core->base_display = display;
        core->buckets = std::move(buckets);
        core->quantiles = std::move(quantiles);
        core->states = std::move(states);
        if (!unit.empty()) {
            core->has_unit = true;
            core->unit_from_dimval = unit.from_dimval;
            core->unit_name = std::string(unit.name);
            core->unit_kind = std::string(unit.kind);
            core->unit_symbol = std::string(unit.symbol);
        }
        {
            const std::scoped_lock lock(mutex_);
            metrics_.push_back(core);
        }
        // name / const_labels / display stay empty here: bind() fills them from
        // the live scope config on first use and on every reconfiguration.
        return Metric{std::move(core)};
    }

    // The live prefix / labels / display this scope applies; shared engine with
    // a decorating Registry. Carries its own mutex and version.
    detail::DecorationState deco_;
    // Guards `metrics_` only (the decoration locks itself).
    mutable std::mutex mutex_;
    // Metrics created through this scope, tracked at creation for enumeration.
    mutable std::vector<std::weak_ptr<MetricCore>> metrics_;
};

namespace detail {

inline std::mutex& scope_map_mutex() {
    static std::mutex mutex;
    return mutex;
}

inline std::unordered_map<std::string, std::shared_ptr<Scope>>& scope_map() {
    static std::unordered_map<std::string, std::shared_ptr<Scope>> map;
    return map;
}

}  // namespace detail

/// Get-or-create the process-wide scope named `name`, using `config` only if it
/// does not yet exist (a later call returns the existing scope and ignores
/// `config` — reconfigure through the returned scope's setters instead).
[[nodiscard]] inline std::shared_ptr<Scope> scope(const std::string_view name, ScopeConfig config) {
    const std::scoped_lock lock(detail::scope_map_mutex());
    auto& map = detail::scope_map();
    const auto key = std::string(name);
    if (const auto it = map.find(key); it != map.end()) {
        return it->second;
    }
    auto created = std::make_shared<Scope>(std::move(config));
    map.emplace(key, created);
    return created;
}

/// Get-or-create the scope named `name` with a default config whose prefix is
/// `name + "_"`.
[[nodiscard]] inline std::shared_ptr<Scope> scope(const std::string_view name) {
    {
        const std::scoped_lock lock(detail::scope_map_mutex());
        auto& map = detail::scope_map();
        if (const auto it = map.find(std::string(name)); it != map.end()) {
            return it->second;
        }
    }
    ScopeConfig config;
    config.prefix = std::string(name) + "_";
    return scope(name, std::move(config));
}

/// Return the scope named `name`, or `nullptr` if none has been created.
[[nodiscard]] inline std::shared_ptr<Scope> find_scope(const std::string_view name) {
    const std::scoped_lock lock(detail::scope_map_mutex());
    auto& map = detail::scope_map();
    const auto it = map.find(std::string(name));
    return it != map.end() ? it->second : nullptr;
}

/// Every process-wide scope created so far (unordered).
[[nodiscard]] inline std::vector<std::shared_ptr<Scope>> scopes() {
    const std::scoped_lock lock(detail::scope_map_mutex());
    const auto& map = detail::scope_map();
    std::vector<std::shared_ptr<Scope>> out;
    out.reserve(map.size());
    for (const auto& scope : map | std::views::values) {
        out.push_back(scope);
    }
    return out;
}

/// The names of every process-wide scope created so far (unordered).
[[nodiscard]] inline std::vector<std::string> scope_names() {
    const std::scoped_lock lock(detail::scope_map_mutex());
    const auto& map = detail::scope_map();
    std::vector<std::string> out;
    out.reserve(map.size());
    for (const auto& name : map | std::views::keys) {
        out.push_back(name);
    }
    return out;
}

// Registry::scope is declared in <prom/registry.hpp> and defined here so the
// registry header need not see the full Scope definition.
inline std::shared_ptr<Scope> Registry::scope(const std::string_view name) {
    return ::prom::scope(name);
}
inline std::shared_ptr<Scope> Registry::scope(const std::string_view name, ScopeConfig config) {
    return ::prom::scope(name, std::move(config));
}

}  // namespace prom
