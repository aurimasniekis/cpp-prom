#pragma once

/// @file
/// @brief A test-only `Adapter` that records everything it is told, so tests
///        can assert that metric mutations reach the backend with the right
///        values, labels, and units.

#include <prom/prom.hpp>

#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prom::test {

/// Backend state that simply remembers what happened to it.
struct RecordingState : prom::MetricState {
    prom::MetricType type{};
    std::string name;
    bool is_family = true;
    prom::Labels const_labels;    ///< Family constant labels.
    prom::Labels dynamic_labels;  ///< Child-only dynamic labels.

    std::vector<std::pair<std::string, double>> ops;  ///< inc/dec/set/observe log.
    std::vector<prom::Label> info_labels;             ///< Last set_info payload.
    std::map<std::string, bool, std::less<>> states;  ///< Latest per-state booleans.
    prom::Unit unit;                                  ///< Latched/declared unit.
    bool unit_set = false;

    // GCC (through at least 14) emits a false-positive -Wnull-dereference for the
    // inlined vector iteration below under -O3; the data pointer cannot be proven
    // non-null after inlining. The loops are well-formed, so silence it locally
    // (GCC only — Clang would reject the unknown-but-clang-specific spelling).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

    [[nodiscard]] double total(std::string_view op) const {
        double sum = 0.0;
        for (const auto& [op_name, op_value] : ops) {
            if (op_name == op) {
                sum += op_value;
            }
        }
        return sum;
    }

    [[nodiscard]] std::size_t count(std::string_view op) const {
        std::size_t n = 0;
        for (const auto& [op_name, op_value] : ops) {
            (void)op_value;
            if (op_name == op) {
                ++n;
            }
        }
        return n;
    }

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
};

/// A fully-functional fake backend. Thread-safe enough for tests.
class RecordingAdapter : public prom::Adapter {
public:
    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "recording";
    }

    [[nodiscard]] prom::MetricHandle
    register_metric(const prom::MetricMeta& meta) noexcept override {
        const std::scoped_lock lock(mutex_);
        auto state = std::make_shared<RecordingState>();
        state->type = meta.type;
        state->name = std::string(meta.name);
        state->is_family = true;
        state->const_labels = meta.const_labels;
        if (!meta.unit.empty()) {
            state->unit = meta.unit;
            state->unit_set = true;
        }
        families_.push_back(state);
        ++register_count_;
        return state;
    }

    [[nodiscard]] prom::MetricHandle resolve(const prom::MetricHandle& family,
                                             const prom::Labels& dynamic) noexcept override {
        const std::scoped_lock lock(mutex_);
        auto* parent = dynamic_cast<RecordingState*>(family.get());
        if (parent == nullptr) {
            return family;
        }
        const std::size_t key = dynamic.hash();
        auto& cache = children_[parent];
        if (const auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
        auto child = std::make_shared<RecordingState>();
        child->type = parent->type;
        child->name = parent->name;
        child->is_family = false;
        child->const_labels = parent->const_labels;
        child->dynamic_labels = dynamic;
        cache.emplace(key, child);
        return child;
    }

    void inc(const prom::MetricHandle& handle, double amount) noexcept override {
        push(handle, "inc", amount);
    }
    void dec(const prom::MetricHandle& handle, double amount) noexcept override {
        push(handle, "dec", amount);
    }
    void set(const prom::MetricHandle& handle, double value) noexcept override {
        push(handle, "set", value);
    }
    void observe(const prom::MetricHandle& handle, double value) noexcept override {
        push(handle, "observe", value);
    }

    void set_info(const prom::MetricHandle& handle,
                  std::span<const prom::Label> labels) noexcept override {
        const std::scoped_lock lock(mutex_);
        if (auto* s = dynamic_cast<RecordingState*>(handle.get())) {
            s->info_labels.assign(labels.begin(), labels.end());
        }
    }

    void set_state(const prom::MetricHandle& handle,
                   std::string_view state,
                   bool active) noexcept override {
        const std::scoped_lock lock(mutex_);
        if (auto* s = dynamic_cast<RecordingState*>(handle.get())) {
            s->states[std::string(state)] = active;
        }
    }

    void set_unit(const prom::MetricHandle& handle, const prom::Unit& unit) noexcept override {
        const std::scoped_lock lock(mutex_);
        if (auto* s = dynamic_cast<RecordingState*>(handle.get())) {
            s->unit = unit;
            s->unit_set = true;
        }
    }

    // --- test inspection ----------------------------------------------------

    [[nodiscard]] std::shared_ptr<RecordingState> family(std::string_view name) const {
        const std::scoped_lock lock(mutex_);
        for (const auto& f : families_) {
            if (f->name == name) {
                return f;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::size_t register_count() const {
        const std::scoped_lock lock(mutex_);
        return register_count_;
    }

private:
    void push(const prom::MetricHandle& handle, std::string_view op, double value) {
        const std::scoped_lock lock(mutex_);
        if (auto* s = dynamic_cast<RecordingState*>(handle.get())) {
            s->ops.emplace_back(std::string(op), value);
        }
    }

    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<RecordingState>> families_;
    std::map<RecordingState*, std::map<std::size_t, std::shared_ptr<RecordingState>>> children_;
    std::size_t register_count_ = 0;
};

}  // namespace prom::test
