/// @file
/// @brief Live adapter migration: swapping a source's adapter re-registers an
///        existing metric against the new backend on its next use, while the
///        old backend's series is left behind. Labeled children are pinned and
///        never migrate.

#include <prom/registry.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::Registry;
using prom::test::RecordingAdapter;

namespace {

class Migration : public ::testing::Test {
protected:
    void TearDown() override {
        Registry::global()->set_adapter(nullptr);
    }
};

}  // namespace

TEST_F(Migration, RegistrySwapReRegistersOnNextUse) {
    const auto first = std::make_shared<RecordingAdapter>();
    const auto registry = Registry::create(first);

    auto counter = registry->counter({.name = "c_total", .help = "h"});
    counter.inc(2.0);
    ASSERT_NE(first->family("c_total"), nullptr);
    EXPECT_DOUBLE_EQ(first->family("c_total")->total("inc"), 2.0);

    const auto second = std::make_shared<RecordingAdapter>();
    registry->set_adapter(second);

    // Not re-registered against the new backend until the metric is used again.
    EXPECT_EQ(second->family("c_total"), nullptr);

    counter.inc(3.0);  // migrates: re-registers against `second`

    ASSERT_NE(second->family("c_total"), nullptr);
    EXPECT_DOUBLE_EQ(second->family("c_total")->total("inc"), 3.0);

    // The previous backend's series is orphaned, untouched by later samples.
    EXPECT_DOUBLE_EQ(first->family("c_total")->total("inc"), 2.0);
}

TEST_F(Migration, GlobalCellSwapMigratesExistingMetric) {
    const auto first = std::make_shared<RecordingAdapter>();
    Registry::global()->set_adapter(first);

    auto gauge = prom::gauge({.name = "mg", .help = "h"});
    gauge.set(1.0);
    ASSERT_NE(first->family("mg"), nullptr);
    EXPECT_DOUBLE_EQ(first->family("mg")->total("set"), 1.0);

    const auto second = std::make_shared<RecordingAdapter>();
    Registry::global()->set_adapter(second);
    gauge.set(2.0);

    ASSERT_NE(second->family("mg"), nullptr);
    EXPECT_DOUBLE_EQ(second->family("mg")->total("set"), 2.0);
    EXPECT_DOUBLE_EQ(first->family("mg")->total("set"), 1.0);
}

TEST_F(Migration, LabeledChildrenDoNotMigrate) {
    const auto first = std::make_shared<RecordingAdapter>();
    const auto registry = Registry::create(first);

    const auto counter = registry->counter({.name = "ch_total", .help = "h"});
    auto child = counter.labels(prom::Labels{{"k", "v"}});
    child.inc(1.0);

    const auto second = std::make_shared<RecordingAdapter>();
    registry->set_adapter(second);

    // The pinned child keeps writing to the original backend; nothing is
    // registered against the new one (the family was never re-used either).
    child.inc(2.0);
    EXPECT_EQ(second->register_count(), 0U);
    EXPECT_EQ(second->family("ch_total"), nullptr);
}
