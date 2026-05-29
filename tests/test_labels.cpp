/// @file
/// @brief Tests for label-name validation, ordering, dedup, hashing, and merge.

#include <prom/labels.hpp>

#include <gtest/gtest.h>

#include <string>

using prom::is_valid_label_name;
using prom::is_valid_metric_name;
using prom::Label;
using prom::Labels;

TEST(MetricName, AcceptsValidNames) {
    EXPECT_TRUE(is_valid_metric_name("http_requests_total"));
    EXPECT_TRUE(is_valid_metric_name("_underscore_start"));
    EXPECT_TRUE(is_valid_metric_name("a"));
    EXPECT_TRUE(is_valid_metric_name("Mixed_Case_9"));
}

TEST(MetricName, RejectsInvalidNames) {
    EXPECT_FALSE(is_valid_metric_name(""));
    EXPECT_FALSE(is_valid_metric_name("9starts_with_digit"));
    EXPECT_FALSE(is_valid_metric_name("has-dash"));
    EXPECT_FALSE(is_valid_metric_name("has space"));
    EXPECT_FALSE(is_valid_metric_name("has:colon"));  // prom uses the strict charset
}

TEST(LabelName, RejectsReservedDoubleUnderscore) {
    EXPECT_TRUE(is_valid_label_name("method"));
    EXPECT_TRUE(is_valid_label_name("_single"));
    EXPECT_FALSE(is_valid_label_name("__reserved"));
    EXPECT_FALSE(is_valid_label_name("__"));
    EXPECT_FALSE(is_valid_label_name("1bad"));
}

TEST(Labels, SortedByName) {
    const Labels labels{{"zeta", "1"}, {"alpha", "2"}, {"mu", "3"}};
    const auto view = labels.view();
    ASSERT_EQ(view.size(), 3U);
    EXPECT_EQ(view[0].name, "alpha");
    EXPECT_EQ(view[1].name, "mu");
    EXPECT_EQ(view[2].name, "zeta");
}

TEST(Labels, DedupLastWins) {
    const Labels labels{{"k", "first"}, {"k", "second"}, {"k", "third"}};
    ASSERT_EQ(labels.size(), 1U);
    EXPECT_EQ(labels.view()[0].value, "third");
}

TEST(Labels, SetOverwrites) {
    Labels labels;
    labels.set("a", "1");
    labels.set("a", "2");
    labels.set("b", "9");
    ASSERT_EQ(labels.size(), 2U);
    EXPECT_EQ(labels.view()[0].value, "2");
}

TEST(Labels, HashIsStableAndOrderIndependent) {
    const Labels a{{"x", "1"}, {"y", "2"}};
    const Labels b{{"y", "2"}, {"x", "1"}};
    EXPECT_EQ(a.hash(), b.hash());

    const Labels c{{"x", "1"}, {"y", "3"}};
    EXPECT_NE(a.hash(), c.hash());
}

TEST(Labels, MergedWithOtherWins) {
    const Labels base{{"region", "eu"}, {"tier", "free"}};
    const Labels overlay{{"tier", "paid"}, {"shard", "7"}};
    const Labels merged = base.merged_with(overlay);

    ASSERT_EQ(merged.size(), 3U);
    EXPECT_EQ(merged.view()[0].name, "region");
    EXPECT_EQ(merged.view()[1].name, "shard");
    EXPECT_EQ(merged.view()[2].name, "tier");
    EXPECT_EQ(merged.view()[2].value, "paid");
}

TEST(Labels, StdHashSpecialization) {
    const Labels labels{{"k", "v"}};
    EXPECT_EQ(std::hash<Labels>{}(labels), labels.hash());
}
