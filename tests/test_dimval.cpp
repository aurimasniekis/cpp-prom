/// @file
/// @brief Dimensional-value integration: normalize(), unit inference at
///        registration, and the runtime unit-kind mismatch drop.

#include <prom/dimval.hpp>
#include <prom/registry.hpp>

#include <dimval/dimval.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::normalize;
using prom::Registry;
using prom::test::RecordingAdapter;

TEST(Normalize, ArithmeticPassesThroughWithoutUnit) {
    const auto [value, unit] = normalize(42);
    EXPECT_DOUBLE_EQ(value, 42.0);
    EXPECT_TRUE(unit.empty());
}

TEST(Normalize, UnitValueCarriesUnitDescriptor) {
    const auto [value, unit] = normalize(dimval::SecondValue{2.5});
    EXPECT_DOUBLE_EQ(value, 2.5);
    EXPECT_FALSE(unit.empty());
    EXPECT_EQ(unit.kind, "time");
    EXPECT_TRUE(unit.from_dimval);
}

TEST(Normalize, MeasureValueUsesBaseUnitDescriptor) {
    // Distance is a measure whose base unit is the meter (kind "length").
    const auto [value, unit] = normalize(dimval::DistanceValue{1500.0});
    EXPECT_DOUBLE_EQ(value, 1500.0);
    EXPECT_EQ(unit.kind, "length");
}

TEST(DimvalConcept, ExcludesArithmetic) {
    static_assert(!prom::DimensionalValue<double>);
    static_assert(!prom::DimensionalValue<int>);
    static_assert(prom::DimensionalValue<dimval::SecondValue>);
    static_assert(prom::DimensionalValue<dimval::DistanceValue>);
}

TEST(UnitInference, LatchesFirstObservedUnit) {
    const auto backend = std::make_shared<RecordingAdapter>();
    const std::shared_ptr<Registry> registry = Registry::create(backend);

    auto hist = registry->histogram({.name = "delays", .help = "h"});  // no declared unit
    hist.observe(dimval::SecondValue{0.5});

    const auto family = backend->family("delays");
    EXPECT_TRUE(family->unit_set);
    EXPECT_EQ(family->unit.kind, "time");
    EXPECT_EQ(family->count("observe"), 1U);
}

TEST(UnitInference, DropsMismatchedKind) {
    const auto backend = std::make_shared<RecordingAdapter>();
    const std::shared_ptr<Registry> registry = Registry::create(backend);

    auto hist = registry->histogram({.name = "mixed", .help = "h"});
    hist.observe(dimval::SecondValue{0.5});  // latches "time"
    hist.observe(dimval::MeterValue{3.0});   // "length" — must be dropped

    EXPECT_EQ(backend->family("mixed")->count("observe"), 1U);
}

TEST(UnitInference, MatchingKindIsAccepted) {
    const auto backend = std::make_shared<RecordingAdapter>();
    const std::shared_ptr<Registry> registry = Registry::create(backend);

    auto hist = registry->histogram({.name = "times", .help = "h"});
    hist.observe(dimval::SecondValue{0.5});
    hist.observe(dimval::SecondValue{1.5});

    EXPECT_EQ(backend->family("times")->count("observe"), 2U);
}
