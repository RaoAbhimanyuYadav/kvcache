#include <gtest/gtest.h>
#include <resp_parser.hpp> // Assuming your class definition is here

// Test Fixture class to handle setup and teardown automatically
class RESPParserTest : public ::testing::Test {
protected:
    RESPParser parser;
    RESPObj result;

    void SetUp() override {
        parser.clear();
    }
};

// ==========================================
// STANDALONE TYPE TESTS
// ==========================================

TEST_F(RESPParserTest, ParsesSimpleStringCorrectly) {
    parser.feed("+OK\r\n");
    
    ASSERT_TRUE(parser.try_parse(result));
    ASSERT_TRUE(std::holds_alternative<std::string>(result.value));
    EXPECT_EQ(std::get<std::string>(result.value), "OK");
}

TEST_F(RESPParserTest, ParsesErrorFrameCorrectly) {
    parser.feed("-ERR unknown command\r\n");
    
    ASSERT_TRUE(parser.try_parse(result));
    ASSERT_TRUE(std::holds_alternative<std::string>(result.value));
    EXPECT_EQ(std::get<std::string>(result.value), "Error: ERR unknown command");
}

TEST_F(RESPParserTest, ParsesIntegerFrameCorrectly) {
    parser.feed(":42\r\n");
    
    ASSERT_TRUE(parser.try_parse(result));
    ASSERT_TRUE(std::holds_alternative<int64_t>(result.value));
    EXPECT_EQ(std::get<int64_t>(result.value), 42);
}

TEST_F(RESPParserTest, ParsesBulkStringCorrectly) {
    parser.feed("$5\r\nhello\r\n");
    
    ASSERT_TRUE(parser.try_parse(result));
    ASSERT_TRUE(std::holds_alternative<std::string>(result.value));
    EXPECT_EQ(std::get<std::string>(result.value), "hello");
}

TEST_F(RESPParserTest, ParsesNullBulkStringCorrectly) {
    parser.feed("$-1\r\n");
    
    ASSERT_TRUE(parser.try_parse(result));
    EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(result.value));
}

// ==========================================
// STREAM FRAGMENTATION TESTS
// ==========================================

TEST_F(RESPParserTest, HandlesIncompleteDataAndRecoversOnNextFeed) {
    // Target command: *3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
    std::string first_chunk  = "*3\r\n$3\r\nSET\r\n$3\r\nfo"; // Cuts off mid "foo" payload
    std::string second_chunk = "o\r\n$3\r\nbar\r\n";          // Fixes buffer bounds

    // Step 1: Feed partial data
    parser.feed(first_chunk);
    
    // ASSERTION: Must return false, indicating a full RESP frame is not yet built
    EXPECT_FALSE(parser.try_parse(result)) << "Parser incorrectly claimed a partial array frame was complete.";

    // Step 2: Feed the remaining bytes to repair stream state
    parser.feed(second_chunk);

    // ASSERTION: Must return true now that frame borders line up safely
    ASSERT_TRUE(parser.try_parse(result)) << "Parser failed to parse the frame even after appending the second half.";

    // Step 3: Deep validate inner structured elements
    ASSERT_TRUE(std::holds_alternative<RESPArray>(result.value));
    RESPArray cmd_tokens = std::get<RESPArray>(result.value);
    
    ASSERT_EQ(cmd_tokens.size(), 3);
    
    EXPECT_EQ(std::get<std::string>(cmd_tokens[0].value), "SET");
    EXPECT_EQ(std::get<std::string>(cmd_tokens[1].value), "foo");
    EXPECT_EQ(std::get<std::string>(cmd_tokens[2].value), "bar");
}

// Standard main function for Google Test execution
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
