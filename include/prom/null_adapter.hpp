#pragma once

/// @file
/// @brief `NullAdapter` — the always-available no-op backend.
///
/// `NullAdapter` is what a library binds to when the host application has not
/// installed a real backend. Every definition and mutation becomes a logged
/// no-op, so a library can declare and exercise metrics unconditionally. It is
/// stateless after construction and therefore fully thread-safe.

#include <prom/adapter.hpp>

#include <logman/logman.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace prom {

/// A backend that records nothing. Registration and resolution log at debug
/// and hand back a single shared inert `MetricState`; mutations log at trace.
/// Logging is null-guarded so construction before any logger setup is safe.
class NullAdapter final : public Adapter {
public:
    NullAdapter() : logger_(logman::get("prom.null")), inert_(std::make_shared<MetricState>()) {}

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "null";
    }

    [[nodiscard]] MetricHandle register_metric(const MetricMeta& meta) noexcept override {
        if (logger_) {
            logger_->debug(
                "register_metric type={} name={} (no-op)", to_string(meta.type), meta.name);
        }
        return inert_;
    }

    [[nodiscard]] MetricHandle resolve(const MetricHandle& /*family*/,
                                       const Labels& dynamic) noexcept override {
        if (logger_) {
            logger_->trace("resolve labels={} (no-op)", dynamic.size());
        }
        return inert_;
    }

    void inc(const MetricHandle& /*handle*/, const double amount) noexcept override {
        trace("inc", amount);
    }

    void dec(const MetricHandle& /*handle*/, const double amount) noexcept override {
        trace("dec", amount);
    }

    void set(const MetricHandle& /*handle*/, const double value) noexcept override {
        trace("set", value);
    }

    void observe(const MetricHandle& /*handle*/, const double value) noexcept override {
        trace("observe", value);
    }

    void set_info(const MetricHandle& /*handle*/,
                  const std::span<const Label> labels) noexcept override {
        if (logger_) {
            logger_->trace("set_info labels={} (no-op)", labels.size());
        }
    }

    void set_state(const MetricHandle& /*handle*/,
                   std::string_view state,
                   bool active) noexcept override {
        if (logger_) {
            logger_->trace("set_state {}={} (no-op)", state, active);
        }
    }

    void set_unit(const MetricHandle& /*handle*/, const Unit& unit) noexcept override {
        if (logger_) {
            logger_->trace("set_unit {} (no-op)", unit.name);
        }
    }

private:
    void trace(std::string_view op, double value) const noexcept {
        if (logger_) {
            logger_->trace("{} {} (no-op)", op, value);
        }
    }

    std::shared_ptr<spdlog::logger> logger_;
    MetricHandle inert_;
};

}  // namespace prom
