#include <gtest/gtest.h>

#include <query_executer.hpp>

#include <string>
#include <vector>

class QueryExecuterTest : public ::testing::Test {
protected:
    QueryExecuter executor;
};

TEST_F(QueryExecuterTest, PingReturnsPong) {
    EXPECT_EQ(executor.execute({"PING"}), "+PONG\r\n");
}

TEST_F(QueryExecuterTest, PingRejectsUnexpectedArguments) {
    EXPECT_EQ(executor.execute({"PING", "extra"}),
              "-ERR Wrong Number of argument.\r\n");
}

TEST_F(QueryExecuterTest, SetThenGetReturnsStoredValue) {
    EXPECT_EQ(executor.execute({"SET", "key", "value"}), "+OK\r\n");
    EXPECT_EQ(executor.execute({"GET", "key"}), "$5\r\nvalue\r\n");
}

TEST_F(QueryExecuterTest, GetMissingKeyReturnsNullBulkString) {
    EXPECT_EQ(executor.execute({"GET", "missing"}), "$-1\r\n");
}

TEST_F(QueryExecuterTest, GetEncodesValueLengthAndEmbeddedCrlf) {
    const std::string value = "a\r\nb";
    ASSERT_EQ(executor.execute({"SET", "key", value}), "+OK\r\n");
    EXPECT_EQ(executor.execute({"GET", "key"}), "$4\r\n" + value + "\r\n");
}

TEST_F(QueryExecuterTest, ExistsAndDeleteReflectStoreState) {
    EXPECT_EQ(executor.execute({"EXISTS", "key"}), ":0\r\n");
    ASSERT_EQ(executor.execute({"SET", "key", "value"}), "+OK\r\n");
    EXPECT_EQ(executor.execute({"EXISTS", "key"}), ":1\r\n");
    EXPECT_EQ(executor.execute({"DEL", "key"}), ":1\r\n");
    EXPECT_EQ(executor.execute({"DEL", "key"}), ":0\r\n");
    EXPECT_EQ(executor.execute({"EXISTS", "key"}), ":0\r\n");
}

TEST_F(QueryExecuterTest, EmptyValueIsDifferentFromMissingKey) {
    ASSERT_EQ(executor.execute({"SET", "key", ""}), "+OK\r\n");
    EXPECT_EQ(executor.execute({"GET", "key"}), "$0\r\n\r\n");
    EXPECT_EQ(executor.execute({"GET", "missing"}), "$-1\r\n");
}

TEST_F(QueryExecuterTest, AcceptsMaximumConfiguredKeyAndValueLengths) {
    const std::string key(KEY_MAX_SIZE, 'k');
    const std::string value(VALUE_MAX_SIZE, 'v');

    EXPECT_EQ(executor.execute({"SET", key, value}), "+OK\r\n");
    EXPECT_EQ(executor.execute({"GET", key}),
              "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n");
}

TEST_F(QueryExecuterTest, RejectsKeysAndValuesOverConfiguredLimits) {
    EXPECT_EQ(executor.execute({"SET", std::string(KEY_MAX_SIZE + 1, 'k'), "v"}),
              "-ERR Invalid argument Size.\r\n");
    EXPECT_EQ(executor.execute({"SET", "key", std::string(VALUE_MAX_SIZE + 1, 'v')}),
              "-ERR Invalid argument Size.\r\n");
    EXPECT_EQ(executor.execute({"GET", std::string(KEY_MAX_SIZE + 1, 'k')}),
              "-ERR Invalid argument Size.\r\n");
}

TEST_F(QueryExecuterTest, RejectsIncorrectArgumentCounts) {
    const std::string expected = "-ERR Wrong Number of argument.\r\n";

    EXPECT_EQ(executor.execute({}), expected);
    EXPECT_EQ(executor.execute({"SET", "key"}), expected);
    EXPECT_EQ(executor.execute({"SET", "key", "value", "extra"}), expected);
    EXPECT_EQ(executor.execute({"GET"}), expected);
    EXPECT_EQ(executor.execute({"DEL", "key", "extra"}), expected);
    EXPECT_EQ(executor.execute({"EXISTS"}), expected);
}

TEST_F(QueryExecuterTest, UnknownCommandReturnsRespError) {
    EXPECT_EQ(executor.execute({"NOPE"}), "-ERR Unsupported Command 'NOPE'.\r\n");
}

TEST_F(QueryExecuterTest, ParserErrorCommandIsForwardedAsRespError) {
    EXPECT_EQ(executor.execute({"ERR", "Malformed RESP"}), "-ERR Malformed RESP\r\n");
}
