#pragma once

/// @file
/// @brief `CompositeAdapter` — fan one metric out to several backends at once.
///
/// A `CompositeAdapter` holds a fixed list of child adapters and forwards every
/// `Adapter` call to each of them. Install it like any other backend
/// (`set_adapter(std::make_shared<CompositeAdapter>(...))`) to, for example,
/// feed both a real prometheus-cpp backend and a test-recording backend from a
/// single set of metrics, or to tee metrics to two exporters during a
/// migration.
///
/// The wrinkle is that `register_metric`/`resolve` each return *one*
/// `MetricHandle`, but every child hands back its own. The composite solves
/// this by bundling the per-child handles into a `CompositeState`: the handle
/// prom holds is a `CompositeState` whose `handles[i]` is the handle child `i`
/// returned. Each mutation then dispatches `handles[i]` to child `i`.

#include <prom/adapter.hpp>
#include <prom/fwd.hpp>
#include <prom/labels.hpp>
#include <prom/unit.hpp>

#include <logman/logman.hpp>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace prom {

/// Backend state for a composite family or child: the index-aligned bundle of
/// the handles each wrapped adapter returned. `handles[i]` belongs to the
/// composite's `i`-th adapter.
class CompositeState final : public MetricState {
public:
    std::vector<MetricHandle> handles;  ///< Per-child handles, child-index aligned.
};

/// A backend that forwards every call to a fixed set of child adapters.
///
/// The adapter list is captured at construction and never mutated afterwards,
/// so — given that each child honours the `Adapter` threading contract — the
/// composite itself needs no locking and is safe to call from any thread. Null
/// entries in the supplied list are dropped so child indices stay dense.
class CompositeAdapter final : public Adapter {
public:
    /// Construct from an explicit list of adapters. Null entries are ignored.
    explicit CompositeAdapter(std::vector<AdapterPtr> adapters)
        : logger_(logman::get("prom.composite")) {
        adapters_.reserve(adapters.size());
        for (auto& a : adapters) {
            if (a) {
                adapters_.push_back(std::move(a));
            }
        }
        if (logger_) {
            logger_->debug("composite backend created with {} adapter(s)", adapters_.size());
        }
    }

    /// Convenience constructor: `CompositeAdapter{a, b, c}`.
    CompositeAdapter(const std::initializer_list<AdapterPtr> adapters)
        : CompositeAdapter(std::vector<AdapterPtr>(adapters)) {}

    /// The wrapped adapters, in dispatch order (nulls already removed).
    [[nodiscard]] std::span<const AdapterPtr> adapters() const noexcept {
        return adapters_;
    }

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "composite";
    }

    [[nodiscard]] MetricHandle register_metric(const MetricMeta& meta) noexcept override {
        auto state = std::make_shared<CompositeState>();
        state->handles.reserve(adapters_.size());
        for (const auto& a : adapters_) {
            state->handles.push_back(a->register_metric(meta));
        }
        return state;
    }

    [[nodiscard]] MetricHandle resolve(const MetricHandle& family,
                                       const Labels& dynamic) noexcept override {
        const auto* parent = dynamic_cast<CompositeState*>(family.get());
        if (parent == nullptr || parent->handles.size() != adapters_.size()) {
            return family;  // Not one of ours — never hand back null.
        }
        auto state = std::make_shared<CompositeState>();
        state->handles.reserve(adapters_.size());
        for (std::size_t i = 0; i < adapters_.size(); ++i) {
            state->handles.push_back(adapters_[i]->resolve(parent->handles[i], dynamic));
        }
        return state;
    }

    void inc(const MetricHandle& handle, const double amount) noexcept override {
        fan_out(handle, [&](Adapter& a, const MetricHandle& h) { a.inc(h, amount); });
    }

    void dec(const MetricHandle& handle, const double amount) noexcept override {
        fan_out(handle, [&](Adapter& a, const MetricHandle& h) { a.dec(h, amount); });
    }

    void set(const MetricHandle& handle, const double value) noexcept override {
        fan_out(handle, [&](Adapter& a, const MetricHandle& h) { a.set(h, value); });
    }

    void observe(const MetricHandle& handle, const double value) noexcept override {
        fan_out(handle, [&](Adapter& a, const MetricHandle& h) { a.observe(h, value); });
    }

    void set_info(const MetricHandle& handle,
                  const std::span<const Label> labels) noexcept override {
        fan_out(handle, [&](Adapter& a, const MetricHandle& h) { a.set_info(h, labels); });
    }

    void set_state(const MetricHandle& handle,
                   const std::string_view state,
                   const bool active) noexcept override {
        fan_out(handle, [&](Adapter& a, const MetricHandle& h) { a.set_state(h, state, active); });
    }

    void set_unit(const MetricHandle& handle, const Unit& unit) noexcept override {
        fan_out(handle, [&](Adapter& a, const MetricHandle& h) { a.set_unit(h, unit); });
    }

private:
    /// Dispatch `fn` to each child paired with that child's handle. Silently
    /// no-ops when `handle` is not a `CompositeState` (e.g. an inert handle from
    /// a fallback path), keeping every mutation non-throwing.
    template <class Fn>
    void fan_out(const MetricHandle& handle, Fn&& fn) noexcept {
        auto* state = dynamic_cast<CompositeState*>(handle.get());
        if (state == nullptr) {
            return;
        }
        const std::size_t n = std::min(adapters_.size(), state->handles.size());
        for (std::size_t i = 0; i < n; ++i) {
            fn(*adapters_[i], state->handles[i]);
        }
    }

    std::shared_ptr<spdlog::logger> logger_;
    std::vector<AdapterPtr> adapters_;
};

}  // namespace prom
