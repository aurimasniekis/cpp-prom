#pragma once

/// @file
/// @brief `StateSet` — a set of mutually-related boolean states, and `StateSetSpec`.

#include <prom/metric_base.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prom {

/// Declarative description of a state set. `states` enumerates the possible
/// member names (e.g. `{"starting", "running", "stopped"}`).
struct StateSetSpec {
    std::string_view name{};
    std::string_view help{};
    Labels labels{};
    comms::DisplayInfo display{};
    std::vector<std::string> states{};
};

/// A state set: each declared state is a boolean, exposed as one series per
/// state with value 0 or 1.
class StateSet : public MetricBase<StateSet> {
public:
    StateSet(const std::string_view name, const std::string_view help)
        : MetricBase(MetricType::StateSet, name, help) {}

    explicit StateSet(const StateSetSpec& spec)
        : MetricBase(MetricType::StateSet, spec.name, spec.help) {
        core_->base_labels = spec.labels;
        core_->const_labels = spec.labels;
        core_->base_display = spec.display;
        core_->display = spec.display;
        core_->states = spec.states;
    }

    explicit StateSet(std::shared_ptr<MetricCore> core) : MetricBase(std::move(core)) {}

    /// Set the boolean value of one state.
    void set(const std::string_view state, const bool active) const noexcept {
        const auto [adapter, handle] = this->bind();
        adapter->set_state(handle, state, active);
    }

    [[nodiscard]] StateSet labels(const Labels& dynamic) const noexcept {
        return make_child(dynamic);
    }
};

}  // namespace prom
