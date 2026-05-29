/// @file
/// @brief Standalone (unbound) metrics: usable before any adapter is installed,
///        lazy binding on first use, and copies sharing the same series.

#include <prom/counter.hpp>
#include <prom/null_adapter.hpp>
#include <prom/registry.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::Counter;
using prom::test::RecordingAdapter;

namespace {

class Standalone : public ::testing::Test {
protected:
    void TearDown() override {
        prom::Registry::global()->set_adapter(nullptr);
    }
};

}  // namespace

TEST_F(Standalone, WorksBeforeAnyAdapterInstalled) {
    // No backend installed: binds to NullAdapter, never throws.
    Counter counter{"orphan_total", "a metric with no backend"};
    EXPECT_NO_THROW(counter.inc());
    EXPECT_NO_THROW(counter.inc(5.0));
}

TEST_F(Standalone, LazilyBindsToInstalledAdapter) {
    const auto backend = std::make_shared<RecordingAdapter>();
    prom::Registry::global()->set_adapter(backend);

    Counter counter{"lazy_total", "binds on first use"};
    // Not registered until first use.
    EXPECT_EQ(backend->register_count(), 0U);

    counter.inc(3.0);
    EXPECT_EQ(backend->register_count(), 1U);
    EXPECT_DOUBLE_EQ(backend->family("lazy_total")->total("inc"), 3.0);
}

TEST_F(Standalone, CopiesShareTheSameSeries) {
    const auto backend = std::make_shared<RecordingAdapter>();
    prom::Registry::global()->set_adapter(backend);

    Counter original{"shared_total", "h"};
    Counter copy = original;  // copy before binding

    original.inc(1.0);  // first use binds the shared core
    copy.inc(2.0);      // same series, no second registration

    EXPECT_EQ(backend->register_count(), 1U);
    EXPECT_DOUBLE_EQ(backend->family("shared_total")->total("inc"), 3.0);
}

TEST_F(Standalone, FromSpecConvenienceCtor) {
    const auto backend = std::make_shared<RecordingAdapter>();
    prom::Registry::global()->set_adapter(backend);

    const Counter counter{prom::CounterSpec{.name = "spec_total", .help = "h"}};
    counter.inc();
    EXPECT_DOUBLE_EQ(backend->family("spec_total")->total("inc"), 1.0);
}
