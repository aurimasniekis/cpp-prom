#pragma once

/// @file
/// @brief Error model for prom: `ErrorCode`, `Error`, `expected`, `Exception`.
///
/// **Policy.** Definition and registration of a metric *validate* their inputs.
/// The throwing factories on `Registry` (`counter`, `gauge`, ...) raise a
/// `prom::Exception` on bad input; their `noexcept` mirrors (`try_counter`,
/// ...) return a `prom::expected` instead. Once a metric exists, **every
/// mutation is `noexcept`** — invalid samples (a negative counter increment, a
/// NaN observation, a unit-kind mismatch) are dropped and logged rather than
/// thrown. The no-client path never throws because a metric always resolves to
/// at least the `NullAdapter`.

#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace prom {

/// Why a metric definition or registration was rejected.
enum class ErrorCode : std::uint8_t {
    InvalidMetricName,   ///< Name violates `[a-zA-Z_][a-zA-Z0-9_]*`.
    InvalidLabelName,    ///< A label name is invalid or uses the reserved `__` prefix.
    EmptyHelp,           ///< Help text is required but was empty.
    InvalidBuckets,      ///< Histogram buckets are unsorted, empty, or non-finite.
    InvalidQuantiles,    ///< Summary quantiles fall outside the open interval (0, 1).
    EmptyStateSet,       ///< A state set was declared with no states.
    RegistrationFailed,  ///< The backend refused to register the metric.
};

/// Human-readable spelling of an `ErrorCode`.
[[nodiscard]] constexpr std::string_view to_string(const ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::InvalidMetricName:
        return "invalid metric name";
    case ErrorCode::InvalidLabelName:
        return "invalid label name";
    case ErrorCode::EmptyHelp:
        return "empty help text";
    case ErrorCode::InvalidBuckets:
        return "invalid histogram buckets";
    case ErrorCode::InvalidQuantiles:
        return "invalid summary quantiles";
    case ErrorCode::EmptyStateSet:
        return "empty state set";
    case ErrorCode::RegistrationFailed:
        return "metric registration failed";
    }
    return "unknown error";
}

/// A validation failure: a machine code plus a human message.
struct Error {
    ErrorCode code;       ///< Machine-readable reason.
    std::string message;  ///< Context (e.g. the offending name).

    bool operator==(const Error&) const = default;
};

/// `std::expected<T, Error>` — the result type of the `Registry::try_*` family.
template <class T>
using expected = std::expected<T, Error>;

/// Thrown by the throwing `Registry` factories when a spec fails validation.
class Exception : public std::runtime_error {
public:
    explicit Exception(Error error)
        : std::runtime_error(std::string(to_string(error.code)) + ": " + error.message),
          error_(std::move(error)) {}

    /// The structured error that triggered this exception.
    [[nodiscard]] const Error& error() const noexcept {
        return error_;
    }

private:
    Error error_;
};

}  // namespace prom
