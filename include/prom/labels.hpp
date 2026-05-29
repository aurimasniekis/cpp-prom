#pragma once

/// @file
/// @brief Label vocabulary: `Label`, the sorted/deduped `Labels` set, name
///        validation, and an `std::hash<Labels>` specialization.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prom {

/// A single `name="value"` label pair.
struct Label {
    std::string name;
    std::string value;

    bool operator==(const Label&) const = default;
};

/// A metric name is valid when it matches Prometheus's `[a-zA-Z_][a-zA-Z0-9_]*`.
[[nodiscard]] constexpr bool is_valid_metric_name(std::string_view name) noexcept {
    if (name.empty()) {
        return false;
    }
    auto is_alpha = [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    auto is_alnum = [&](const char c) { return is_alpha(c) || (c >= '0' && c <= '9'); };
    if (!is_alpha(name.front())) {
        return false;
    }
    return std::ranges::all_of(name, is_alnum);
}

/// A label name follows the metric-name charset but additionally rejects the
/// `__` prefix, which OpenMetrics reserves for internal use.
[[nodiscard]] constexpr bool is_valid_label_name(const std::string_view name) noexcept {
    if (!is_valid_metric_name(name)) {
        return false;
    }
    return name.size() < 2 || name[0] != '_' || name[1] != '_';
}

/// An immutable-by-convention set of labels, kept sorted by name with
/// duplicates collapsed last-wins. The 64-bit FNV-1a `hash()` is computed once
/// and cached, which makes `Labels` cheap to use as a key for labeled-child
/// caches (see the `std::hash<Labels>` specialization below).
class Labels {
public:
    Labels() = default;

    Labels(const std::initializer_list<Label> init) : labels_(init) {
        normalize();
    }

    explicit Labels(std::vector<Label> labels) : labels_(std::move(labels)) {
        normalize();
    }

    /// Insert or overwrite a label (last write wins on a repeated name).
    void set(std::string name, std::string value) {
        labels_.push_back(Label{std::move(name), std::move(value)});
        normalize();
    }

    /// Read-only view over the sorted/deduped pairs.
    [[nodiscard]] std::span<const Label> view() const noexcept {
        return labels_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return labels_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return labels_.size();
    }

    /// 64-bit FNV-1a hash over the `name\0value\0` stream; computed lazily and
    /// cached. Order-independent in effect because the set is always sorted.
    [[nodiscard]] std::size_t hash() const noexcept {
        if (!hash_valid_) {
            hash_ = compute_hash();
            hash_valid_ = true;
        }
        return hash_;
    }

    /// Return `*this` overlaid with `other`; on a name collision `other` wins.
    [[nodiscard]] Labels merged_with(const Labels& other) const {
        std::vector<Label> combined;
        combined.reserve(labels_.size() + other.labels_.size());
        combined.insert(combined.end(), labels_.begin(), labels_.end());
        combined.insert(combined.end(), other.labels_.begin(), other.labels_.end());
        return Labels{std::move(combined)};
    }

    bool operator==(const Labels& rhs) const {
        return labels_ == rhs.labels_;
    }

private:
    void normalize() {
        std::ranges::stable_sort(labels_, {}, &Label::name);
        // Collapse equal-name runs keeping the last entry (last write wins).
        std::vector<Label> out;
        out.reserve(labels_.size());
        for (std::size_t i = 0; i < labels_.size(); ++i) {
            const bool shadowed_by_next =
                i + 1 < labels_.size() && labels_[i].name == labels_[i + 1].name;
            if (!shadowed_by_next) {
                out.push_back(std::move(labels_[i]));
            }
        }
        labels_ = std::move(out);
        hash_valid_ = false;
    }

    [[nodiscard]] std::size_t compute_hash() const noexcept {
        constexpr std::uint64_t fnv_offset = 1469598103934665603ULL;
        constexpr std::uint64_t fnv_prime = 1099511628211ULL;
        std::uint64_t h = fnv_offset;
        auto mix = [&](const std::string_view s) {
            for (const char c : s) {
                h ^= static_cast<std::uint8_t>(c);
                h *= fnv_prime;
            }
            h ^= 0U;  // explicit field separator
            h *= fnv_prime;
        };
        for (const auto& [name, value] : labels_) {
            mix(name);
            mix(value);
        }
        return static_cast<std::size_t>(h);
    }

    std::vector<Label> labels_;
    mutable std::size_t hash_ = 0;
    mutable bool hash_valid_ = false;
};

}  // namespace prom

/// Hash a `prom::Labels` by its cached FNV-1a digest so it can key the labeled
/// child caches that adapters maintain.
template <>
struct std::hash<prom::Labels> {
    [[nodiscard]] std::size_t operator()(const prom::Labels& labels) const noexcept {
        return labels.hash();
    }
};  // namespace std
