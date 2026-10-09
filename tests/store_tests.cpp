#include <string>
#include <gtest/gtest.h>

#include <store.hpp>


class StoreTest : public ::testing::Test {
protected:
    Store store;
};

TEST_F(StoreTest, StoresAndRetrievesValue) {
    ASSERT_TRUE(store.set_key_value("key1", "val1"));
    EXPECT_EQ(store.get_value("key1"), std::make_pair(true, std::string("val1")));
}

TEST_F(StoreTest, MissingKeyReturnsNotFound) {
    EXPECT_EQ(store.get_value("missing"),
              std::make_pair(false, std::string("No key found")));
    EXPECT_FALSE(store.exists_key("missing"));
}

TEST_F(StoreTest, UpdatingKeyReplacesPreviousValue) {
    ASSERT_TRUE(store.set_key_value("key", "first"));
    ASSERT_TRUE(store.set_key_value("key", "second"));
    EXPECT_EQ(store.get_value("key"), std::make_pair(true, std::string("second")));
}

TEST_F(StoreTest, AcceptsValuesAtConfiguredMaximumLengths) {
    const std::string key(KEY_MAX_SIZE, 'k');
    const std::string value(VALUE_MAX_SIZE, 'v');

    ASSERT_TRUE(store.set_key_value(key, value));
    EXPECT_EQ(store.get_value(key), std::make_pair(true, value));
}

TEST_F(StoreTest, RejectsKeyOrValueLongerThanConfiguredMaximum) {
    EXPECT_FALSE(store.set_key_value(std::string(KEY_MAX_SIZE + 1, 'k'), "value"));
    EXPECT_FALSE(store.set_key_value("key", std::string(VALUE_MAX_SIZE + 1, 'v')));
    EXPECT_FALSE(store.exists_key(std::string(KEY_MAX_SIZE + 1, 'k')));
}

TEST_F(StoreTest, DeletingKeyReturnsWhetherItExisted) {
    ASSERT_TRUE(store.set_key_value("key", "value"));

    EXPECT_TRUE(store.del_key_val("key"));
    EXPECT_FALSE(store.exists_key("key"));
    EXPECT_FALSE(store.del_key_val("key"));
}

TEST_F(StoreTest, EmptyKeyAndValueAreStored) {
    ASSERT_TRUE(store.set_key_value("", ""));
    EXPECT_TRUE(store.exists_key(""));
    EXPECT_EQ(store.get_value(""), std::make_pair(true, std::string{}));
}

TEST_F(StoreTest, SeparateStoreInstancesDoNotShareData) {
    Store another_store;
    ASSERT_TRUE(store.set_key_value("key", "value"));

    EXPECT_FALSE(another_store.exists_key("key"));
}
