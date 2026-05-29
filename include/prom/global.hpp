#pragma once

/// @file
/// @brief The process-wide adapter cell and its install/swap semantics.
///
/// The adapter no longer lives on each metric — it lives on an `AdapterCell`
/// (an `AdapterSource`) that metrics read through. Standalone and scoped metrics
/// share the process-wide cell returned by `detail::global_adapter_cell()`; a
/// `Registry` owns its own cell. Install the real backend on a cell and every
/// metric reading from it migrates to the new backend on its next use:
///
/// @code
/// prom::Registry::global()->set_adapter(std::make_shared<my::Backend>(...));
/// @endcode
///
/// The default adapter is a `NullAdapter`, so metrics are always safe to use
/// before any backend is installed.

#include <prom/adapter.hpp>
#include <prom/null_adapter.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace prom {

/// A swappable adapter slot shared by every metric that reads from it. Holds a
/// `NullAdapter` by default; `set_adapter` installs a new backend and bumps
/// `version()`, which is how already-created metrics learn to re-register
/// against the new adapter on their next use.
class AdapterCell final : public AdapterSource {
public:
    AdapterCell() : adapter_(std::make_shared<NullAdapter>()) {}

    /// Construct with `adapter` (or a fresh `NullAdapter` when null).
    explicit AdapterCell(AdapterPtr adapter)
        : adapter_(adapter ? std::move(adapter) : std::make_shared<NullAdapter>()) {}

    [[nodiscard]] std::uint64_t version() const noexcept override {
        return version_.load(std::memory_order_acquire);
    }

    [[nodiscard]] AdapterPtr adapter() const override {
        const std::scoped_lock lock(mutex_);
        return adapter_;
    }

    /// Install `adapter` as the new backend (or reset to a fresh `NullAdapter`
    /// when null) and bump the version so reading metrics re-register.
    void set_adapter(AdapterPtr adapter) {
        {
            const std::scoped_lock lock(mutex_);
            adapter_ = adapter ? std::move(adapter) : std::make_shared<NullAdapter>();
        }
        version_.fetch_add(1, std::memory_order_release);
    }

private:
    mutable std::mutex mutex_;
    AdapterPtr adapter_;
    std::atomic<std::uint64_t> version_{0};
};

namespace detail {

/// The process-wide adapter cell shared by standalone and scoped metrics and by
/// `Registry::global()`. Function-local static gives an ODR-safe, lazily
/// initialized singleton in a header-only library.
[[nodiscard]] inline std::shared_ptr<AdapterCell> global_adapter_cell() {
    static auto cell = std::make_shared<AdapterCell>();
    return cell;
}

}  // namespace detail

}  // namespace prom
