#include <gtest/gtest.h>
#include <store.hpp>

// Test Fixture class to handle setup and teardown automatically
class StoreTest : public ::testing::Test {
protected:
    Store store;
};

TEST_F(StoreTest, StoreNewKeyCorrectly) {
    std::string key = "key1", val = "val1";
    ASSERT_TRUE(store.set_key_value(key, val));
    
    EXPECT_EQ(store.get_value(key), std::make_pair(true, val));
}
