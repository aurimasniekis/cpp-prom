#pragma once

/// @file
/// @brief `MetricCore` (the shared per-series state) and the CRTP `MetricBase`
///        that gives every metric type value semantics and lazy binding.
///
/// A metric handle is a cheap, copyable value type that holds a
/// `shared_ptr<MetricCore>`. Copies refer to the same series. A metric is
/// created in one of two states:
///
///   * **standalone** — built from just `(name, help)` (or a spec) and *unbound*.
///     On first use it binds to the adapter currently installed on the
///     process-wide cell and registers itself. Copies of an unbound metric all
///     resolve to the same series.
///   * **registered** — built by a `Registry` (or by `labels()`), eagerly
///     registered against its source's current adapter.
///
/// Every core reads its adapter from an `AdapterSource` (the cell of its
/// registry, or the global cell). When that source's `version()` advances — an
/// adapter swap — or, for a scoped metric, when the scope's `version()` advances,
/// the core re-registers against the new adapter/config on its next use. Labeled
/// children are *pinned*: they snapshot their binding at creation and never
/// migrate. After binding, the hot path is a single relaxed load plus a virtual
/// call into the adapter.

#include <prom/adapter.hpp>
#include <prom/dimval.hpp>
#include <prom/global.hpp>
#include <prom/labels.hpp>
#include <prom/unit.hpp>

#include <logman/logman.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prom {

/// Read-only view a *scoped* metric uses to re-resolve its name, constant
/// labels, and display metadata against the live scope configuration on every
/// use. Implemented by `prom::Scope`. It is declared abstractly here so that
/// `metric_base` carries no dependency on `Scope`/`Registry` (which would be a
/// cycle). `version()` advances on every configuration change, which is how a
/// scoped metric notices it must re-register.
class ScopeState {
public:
    ScopeState() = default;
    ScopeState(const ScopeState&) = delete;
    ScopeState& operator=(const ScopeState&) = delete;
    ScopeState(ScopeState&&) = delete;
    ScopeState& operator=(ScopeState&&) = delete;
    virtual ~ScopeState() = default;

    /// Monotonic counter, bumped on every configuration change.
    [[nodiscard]] virtual std::uint64_t version() const noexcept = 0;
    /// The effective metric name for a base (un-prefixed) name.
    [[nodiscard]] virtual std::string full_name(std::string_view base) const = 0;
    /// The effective constant labels for a metric's own labels.
    [[nodiscard]] virtual Labels effective_labels(const Labels& own) const = 0;
    /// The effective display metadata for a metric's own display.
    [[nodiscard]] virtual comms::DisplayInfo
    effective_display(const comms::DisplayInfo& own) const = 0;
    /// Whether this decoration actually changes anything (a non-empty prefix,
    /// constant labels, or display — directly or via a parent it chains onto).
    /// Drives the `scoped` flag on `MetricInfo`: an empty decoration leaves a
    /// metric reported as un-decorated.
    [[nodiscard]] virtual bool decorates() const noexcept = 0;
};

namespace detail {

/// Overlay a metric's own display fields onto a set of defaults; each per-metric
/// field that is set wins over the default. Shared by every `ScopeState`.
[[nodiscard]] inline comms::DisplayInfo merge_display(const comms::DisplayInfo& base,
                                                      const comms::DisplayInfo& over) {
    comms::DisplayInfo out = base;
    if (over.name) {
        out.name = over.name;
    }
    if (over.description) {
        out.description = over.description;
    }
    if (over.icon) {
        out.icon = over.icon;
    }
    if (over.color) {
        out.color = over.color;
    }
    return out;
}

/// Whether a `DisplayInfo` carries any field at all.
[[nodiscard]] inline bool display_empty(const comms::DisplayInfo& display) noexcept {
    return !display.name && !display.description && !display.icon && !display.color;
}

/// A live, thread-safe metric decoration — a name prefix, default constant
/// labels, and default display metadata — with a version that advances on every
/// change. This is the engine behind both a `prom::Scope` and a `prom::Registry`:
/// a metric core that points at one (via `MetricCore::scope`) re-resolves its
/// effective name / labels / display whenever `version()` moves (see
/// `MetricBase::rebind`), so mutating the decoration at runtime reconfigures
/// every metric created against it.
///
/// A decoration may chain onto a `parent` (a scope chains onto the global
/// registry's decoration), in which case the parent is applied *outermost*:
/// `parent.prefix + this.prefix + base`. The metric's own values still win over
/// `this`, which wins over the parent. The version composes both, so a change to
/// either re-registers the metric.
class DecorationState : public ScopeState {
public:
    DecorationState() = default;
    DecorationState(std::string prefix,
                    Labels const_labels,
                    comms::DisplayInfo display,
                    std::shared_ptr<ScopeState> parent = nullptr)
        : prefix_(std::move(prefix)), const_labels_(std::move(const_labels)),
          display_(std::move(display)), parent_(std::move(parent)) {}

    [[nodiscard]] std::uint64_t version() const noexcept override {
        const std::uint64_t self = version_.load(std::memory_order_acquire);
        return parent_ ? self + parent_->version() : self;
    }
    [[nodiscard]] std::string full_name(const std::string_view base) const override {
        std::string out;
        {
            const std::scoped_lock lock(mutex_);
            out = prefix_ + std::string(base);
        }
        return parent_ ? parent_->full_name(out) : out;
    }
    [[nodiscard]] Labels effective_labels(const Labels& own) const override {
        Labels merged;
        {
            const std::scoped_lock lock(mutex_);
            merged = const_labels_.merged_with(own);
        }
        return parent_ ? parent_->effective_labels(merged) : merged;
    }
    [[nodiscard]] comms::DisplayInfo
    effective_display(const comms::DisplayInfo& own) const override {
        comms::DisplayInfo merged;
        {
            const std::scoped_lock lock(mutex_);
            merged = merge_display(display_, own);
        }
        return parent_ ? parent_->effective_display(merged) : merged;
    }
    [[nodiscard]] bool decorates() const noexcept override {
        {
            const std::scoped_lock lock(mutex_);
            if (!prefix_.empty() || !const_labels_.empty() || !display_empty(display_)) {
                return true;
            }
        }
        return parent_ && parent_->decorates();
    }

    [[nodiscard]] std::string prefix() const {
        const std::scoped_lock lock(mutex_);
        return prefix_;
    }
    void set_prefix(std::string prefix) {
        {
            const std::scoped_lock lock(mutex_);
            prefix_ = std::move(prefix);
        }
        bump();
    }
    [[nodiscard]] Labels const_labels() const {
        const std::scoped_lock lock(mutex_);
        return const_labels_;
    }
    void set_const_labels(Labels labels) {
        {
            const std::scoped_lock lock(mutex_);
            const_labels_ = std::move(labels);
        }
        bump();
    }
    void add_const_label(std::string name, std::string value) {
        {
            const std::scoped_lock lock(mutex_);
            const_labels_.set(std::move(name), std::move(value));
        }
        bump();
    }
    [[nodiscard]] comms::DisplayInfo display() const {
        const std::scoped_lock lock(mutex_);
        return display_;
    }
    void set_display(comms::DisplayInfo display) {
        {
            const std::scoped_lock lock(mutex_);
            display_ = std::move(display);
        }
        bump();
    }

    /// Replace prefix, constant labels, and display in one shot (single version
    /// bump).
    void configure(std::string prefix, Labels const_labels, comms::DisplayInfo display) {
        {
            const std::scoped_lock lock(mutex_);
            prefix_ = std::move(prefix);
            const_labels_ = std::move(const_labels);
            display_ = std::move(display);
        }
        bump();
    }

private:
    void bump() noexcept {
        version_.fetch_add(1, std::memory_order_release);
    }

    mutable std::mutex mutex_;
    std::string prefix_;
    Labels const_labels_;
    comms::DisplayInfo display_;
    // Set once at construction (a scope chains onto the global decoration), then
    // only read, so it needs no lock.
    std::shared_ptr<ScopeState> parent_;
    std::atomic<std::uint64_t> version_{1};
};

/// The process-wide decoration shared by `Registry::global()`, every standalone
/// metric, and (as their chain parent) every `Scope`. Installing a prefix or
/// constant labels here — e.g. via `Registry::global()->set_prefix(...)` —
/// therefore reaches every metric in the process on its next use. Empty by
/// default, so it is a no-op until configured.
[[nodiscard]] inline std::shared_ptr<DecorationState> global_decoration() {
    static auto decoration = std::make_shared<DecorationState>();
    return decoration;
}

}  // namespace detail

/// Portable atomic `shared_ptr<const T>` exposing just `load()`/`store()`.
///
/// Uses `std::atomic<std::shared_ptr<...>>` on toolchains that implement it
/// (`__cpp_lib_atomic_shared_ptr`); otherwise falls back to a mutex-guarded
/// `shared_ptr`. Apple's libc++ (as of Xcode 26) ships C++23 without the atomic
/// `shared_ptr` specialization, so the fallback is the live path there.
namespace detail {
template <class T>
class AtomicSharedPtr {
public:
    AtomicSharedPtr() = default;

    [[nodiscard]] std::shared_ptr<const T> load() const noexcept {
#if defined(__cpp_lib_atomic_shared_ptr)
        return value_.load(std::memory_order_acquire);
#else
        const std::scoped_lock lock(mutex_);
        return value_;
#endif
    }

    void store(std::shared_ptr<const T> desired) noexcept {
#if defined(__cpp_lib_atomic_shared_ptr)
        value_.store(std::move(desired), std::memory_order_release);
#else
        const std::scoped_lock lock(mutex_);
        value_ = std::move(desired);
#endif
    }

private:
#if defined(__cpp_lib_atomic_shared_ptr)
    std::atomic<std::shared_ptr<const T>> value_;
#else
    mutable std::mutex mutex_;
    std::shared_ptr<const T> value_;
#endif
};
}  // namespace detail

/// Shared, reference-counted state behind every metric handle. It owns the
/// stable string/buffer storage that `MetricMeta` views into, the adapter
/// source it reads its backend from, the currently-published binding, and the
/// bookkeeping for lazy/live binding and runtime unit reconciliation.
struct MetricCore {
    MetricType type{};

    // Stable storage backing the transient MetricMeta built at registration.
    std::string name;
    std::string help;
    Labels const_labels;
    comms::DisplayInfo display;
    std::vector<double> buckets;
    std::vector<double> quantiles;
    std::vector<std::string> states;

    // Where this core reads its adapter from: the cell of the Registry that
    // created it, or the process-wide global cell (standalone / scoped metrics).
    std::shared_ptr<AdapterSource> source;

    // The currently published binding. Null until first use for a standalone or
    // scoped metric; set eagerly by a Registry. Read on the hot path, replaced
    // wholesale under `bind_mutex` when re-registration is needed, so an
    // in-flight caller's snapshot stays valid.
    struct Bound {
        AdapterPtr adapter;
        MetricHandle handle;
        std::uint64_t adapter_version = 0;
        std::uint64_t scope_version = 0;
    };
    detail::AtomicSharedPtr<Bound> bound;

    // Labeled children snapshot their binding at creation and never migrate.
    bool pinned = false;

    // Scoped metrics: when `scope` is set, the metric stays unbound at creation
    // and (re-)registers whenever the scope's config version advances, so a
    // runtime change to the scope's prefix / labels / display reconfigures every
    // metric created from it. The `base_*` fields hold the metric's own,
    // un-decorated values; `name` / `const_labels` / `display` above hold the
    // decorated values that are currently registered. Guarded by `bind_mutex`.
    std::shared_ptr<ScopeState> scope;
    std::string base_name;
    Labels base_labels;
    comms::DisplayInfo base_display;
    std::mutex bind_mutex;

    // The family this core belongs to: null when this core *is* the family,
    // set to the parent family core for labeled children. Unit reconciliation
    // is always performed against the family.
    std::shared_ptr<MetricCore> family;

    // Runtime unit state, guarded by unit_mutex. `has_unit` is true once a unit
    // is known — either declared in the spec or latched from the first
    // dimensional observation.
    std::mutex unit_mutex;
    bool has_unit = false;
    bool unit_from_dimval = false;
    std::string unit_name;
    std::string unit_kind;
    std::string unit_symbol;

    /// Build a non-owning view of the currently-known unit.
    [[nodiscard]] Unit unit_view() const {
        if (!has_unit) {
            return Unit{};
        }
        return Unit{unit_name, unit_kind, unit_symbol, unit_from_dimval};
    }

    /// Assemble the transient registration metadata. The returned views/spans
    /// are valid only while `*this` outlives the call (always true: the caller
    /// holds the core).
    [[nodiscard]] MetricMeta build_meta() const {
        MetricMeta meta;
        meta.type = type;
        meta.name = name;
        meta.help = help;
        meta.unit = unit_view();
        meta.const_labels = const_labels;
        meta.display = display;
        meta.buckets = buckets;
        meta.quantiles = quantiles;
        meta.states = states;
        return meta;
    }
};

/// CRTP base shared by every metric type. Provides the binding machinery and
/// the protected helpers (`record_*`, `make_child`, `reconcile_unit`) the
/// concrete types build their public surface from.
template <class Derived>
class MetricBase {
public:
    /// The metric's fully-qualified name.
    [[nodiscard]] std::string_view name() const noexcept {
        return core_->name;
    }

    /// The metric kind.
    [[nodiscard]] MetricType type() const noexcept {
        return core_->type;
    }

protected:
    /// Standalone, unbound construction from a name and help string. Reads its
    /// adapter from the process-wide cell and binds lazily on first use. It also
    /// follows the process-wide global decoration, so a prefix / labels installed
    /// via `Registry::global()` reach standalone metrics too; with the default
    /// (empty) decoration the effective name and labels are unchanged.
    MetricBase(const MetricType type, const std::string_view name, const std::string_view help)
        : core_(std::make_shared<MetricCore>()) {
        core_->type = type;
        core_->name = std::string(name);
        core_->base_name = std::string(name);
        core_->help = std::string(help);
        core_->source = detail::global_adapter_cell();
        core_->scope = detail::global_decoration();
    }

    /// Adopt an already-populated core (registered metrics and children).
    explicit MetricBase(std::shared_ptr<MetricCore> core) : core_(std::move(core)) {}

    MetricBase(const MetricBase&) = default;
    MetricBase& operator=(const MetricBase&) = default;
    MetricBase(MetricBase&&) = default;
    MetricBase& operator=(MetricBase&&) = default;
    ~MetricBase() = default;

    /// A resolved (adapter, handle) snapshot — the result of `bind()`. Callers
    /// operate on this copy so a concurrent re-bind of a scoped metric cannot
    /// invalidate the adapter/handle mid-operation.
    struct Binding {
        AdapterPtr adapter;
        MetricHandle handle;
    };

    /// Resolve the adapter and backend handle this metric should record against.
    ///
    /// Fast path: load the published binding. A *pinned* binding (a labeled
    /// child) is returned as-is — children never migrate. Otherwise the binding
    /// is reused while it matches the source's adapter `version()` and, for a
    /// scoped metric, the scope's config `version()`.
    ///
    /// Slow path (`rebind`): resolve the current adapter from the source,
    /// recompute the name / labels / display from the live scope (if scoped),
    /// register the family, and publish a fresh binding. This fires on first use
    /// of a standalone/scoped metric, on an adapter swap (live migration to the
    /// new backend — the previous backend's series are left behind), and on a
    /// scope reconfiguration.
    [[nodiscard]] Binding bind() const noexcept {
        if (const auto b = core_->bound.load()) {
            if (core_->pinned) {
                return Binding{b->adapter, b->handle};
            }
            const std::uint64_t av = core_->source->version();
            if (const std::uint64_t sv = core_->scope ? core_->scope->version() : 0;
                b->adapter_version == av && b->scope_version == sv) {
                return Binding{b->adapter, b->handle};
            }
        }
        return rebind();
    }

    [[nodiscard]] const std::shared_ptr<MetricCore>& core() const noexcept {
        return core_;
    }

    /// Resolve a labeled child of the same metric type. The child carries the
    /// backend's resolved child handle and points back at this family for unit
    /// reconciliation. The child is *pinned*: it snapshots the adapter and the
    /// scope-decorated state in effect at the time of the `labels()` call and
    /// never migrates, even if the source adapter or scope later changes.
    [[nodiscard]] Derived make_child(const Labels& dynamic) const noexcept {
        const Binding b = bind();
        auto child = std::make_shared<MetricCore>();
        child->type = core_->type;
        child->name = core_->name;
        child->source = core_->source;
        child->pinned = true;
        child->family = family_core();
        child->bound.store(std::make_shared<const MetricCore::Bound>(
            MetricCore::Bound{b.adapter, b.adapter->resolve(b.handle, dynamic), 0, 0}));
        return Derived{std::move(child)};
    }

    /// Reconcile an observed unit against the family's known unit. Returns false
    /// (and logs) when a dimensional sample's kind contradicts the latched
    /// kind, signalling the caller to drop the sample. A unitless sample always
    /// passes. The first dimensional sample latches the unit.
    [[nodiscard]] bool reconcile_unit(const Unit& observed, Adapter& adapter) const noexcept {
        if (observed.empty()) {
            return true;
        }
        const std::shared_ptr<MetricCore> fam = family_core();
        const std::scoped_lock lock(fam->unit_mutex);
        if (!fam->has_unit) {
            fam->unit_name = std::string(observed.name);
            fam->unit_kind = std::string(observed.kind);
            fam->unit_symbol = std::string(observed.symbol);
            fam->unit_from_dimval = observed.from_dimval;
            fam->has_unit = true;
            MetricHandle handle;
            if (const auto fb = fam->bound.load()) {
                handle = fb->handle;
            }
            if (!handle) {
                if (const auto cb = core_->bound.load()) {
                    handle = cb->handle;
                }
            }
            adapter.set_unit(handle, fam->unit_view());
            return true;
        }
        if (!fam->unit_kind.empty() && !observed.kind.empty() && fam->unit_kind != observed.kind) {
            if (auto* lg = logger()) {
                lg->warn("metric '{}' dropping sample: unit kind '{}' does not match "
                         "latched kind '{}'",
                         fam->name,
                         observed.kind,
                         fam->unit_kind);
            }
            return false;
        }
        return true;
    }

    /// Drop-and-log guard for a non-finite sample. Returns true when the value
    /// is safe to record.
    [[nodiscard]] bool check_finite(const double value, std::string_view op) const noexcept {
        if (std::isnan(value) || std::isinf(value)) {
            if (auto* lg = logger()) {
                lg->warn("metric '{}' dropping {}: non-finite value", core_->name, op);
            }
            return false;
        }
        return true;
    }

    /// The shared per-process metrics logger.
    [[nodiscard]] static spdlog::logger* logger() noexcept {
        static std::shared_ptr<spdlog::logger> lg = logman::get("prom");
        return lg.get();
    }

    std::shared_ptr<MetricCore> core_;

private:
    [[nodiscard]] std::shared_ptr<MetricCore> family_core() const noexcept {
        return core_->family ? core_->family : core_;
    }

    /// Slow path of `bind()`. Resolves the current adapter, recomputes the
    /// scope-decorated fields (if scoped), registers the family, and publishes a
    /// fresh binding. Double-checked under `bind_mutex` so concurrent callers
    /// register at most once per version.
    [[nodiscard]] Binding rebind() const noexcept {
        const std::scoped_lock lock(core_->bind_mutex);
        const std::uint64_t av = core_->source->version();
        const std::uint64_t sv = core_->scope ? core_->scope->version() : 0;
        if (const auto b = core_->bound.load()) {
            if (core_->pinned || (b->adapter_version == av && b->scope_version == sv)) {
                return Binding{b->adapter, b->handle};
            }
        }
        const AdapterPtr adapter = core_->source->adapter();
        if (core_->scope) {
            core_->name = core_->scope->full_name(core_->base_name);
            core_->const_labels = core_->scope->effective_labels(core_->base_labels);
            core_->display = core_->scope->effective_display(core_->base_display);
        }
        const MetricHandle handle = adapter->register_metric(core_->build_meta());
        core_->bound.store(
            std::make_shared<const MetricCore::Bound>(MetricCore::Bound{adapter, handle, av, sv}));
        return Binding{adapter, handle};
    }
};

}  // namespace prom
