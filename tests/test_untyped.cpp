/// @file
/// @brief Untyped behavior: set with raw and dimensional values.

#include <prom/registry.hpp>
#include <prom/untyped.hpp>

#include <dimval/dimval.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::Registry;
using prom::Untyped;
using prom::test::RecordingAdapter;

namespace {

class UntypedTest : public ::testing::Test {
protected:
    std::shared_ptr<RecordingAdapter> backend = std::make_shared<RecordingAdapter>();
    std::shared_ptr<Registry> registry = Registry::create(backend);
};

}  // namespace

TEST_F(UntypedTest, SetRawValue) {
    auto metric = registry->untyped({.name = "raw", .help = "h"});
    metric.set(42.0);
    metric.set(-7.0);
    const auto family = backend->family("raw");
    ASSERT_NE(family, nullptr);
    EXPECT_DOUBLE_EQ(family->ops.back().second, -7.0);
    EXPECT_EQ(family->count("set"), 2U);
}

TEST_F(UntypedTest, SetDimensionalValue) {
    auto metric = registry->untyped({.name = "v", .help = "h"});
    metric.set(dimval::VoltValue{3.3});
    EXPECT_DOUBLE_EQ(backend->family("v")->total("set"), 3.3);
}
