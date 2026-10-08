#include <gtest/gtest.h>

#include <resp_parser.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace {

ParseResult parse_one(RESPParser& parser, const std::string& frame, RESPObj& out) {
    parser.feed(frame.data(), frame.size());
    return parser.try_parse(out);
}

}  // namespace

class RespParserTest : public ::testing::Test {
protected:
    RESPParser parser;
    RESPObj out;
};


TEST_F(RespParserTest, ParsesSimpleString) {
    EXPECT_EQ(parse_one(parser, "+OK\r\n", out), ParseResult::Success);
    ASSERT_TRUE(std::holds_alternative<std::string>(out.value));
    EXPECT_EQ(std::get<std::string>(out.value), "OK");
}

TEST_F(RespParserTest, ParsesErrorAsStringWithErrorPrefix) {
    EXPECT_EQ(parse_one(parser, "-ERR bad request\r\n", out), ParseResult::Success);
    ASSERT_TRUE(std::holds_alternative<std::string>(out.value));
    EXPECT_EQ(std::get<std::string>(out.value), "Error: ERR bad request");
}

TEST_F(RespParserTest, ParsesInteger) {
    EXPECT_EQ(parse_one(parser, ":-42\r\n", out), ParseResult::Success);
    ASSERT_TRUE(std::holds_alternative<std::int64_t>(out.value));
    EXPECT_EQ(std::get<std::int64_t>(out.value), -42);
}

TEST_F(RespParserTest, ParsesBulkStringIncludingEmbeddedCrlf) {
    EXPECT_EQ(parse_one(parser, "$5\r\na\r\nbc\r\n", out), ParseResult::Success);
    ASSERT_TRUE(std::holds_alternative<std::string>(out.value));
    EXPECT_EQ(std::get<std::string>(out.value), "a\r\nbc");
}

TEST_F(RespParserTest, ParsesNullBulkString) {
    EXPECT_EQ(parse_one(parser, "$-1\r\n", out), ParseResult::Success);
    EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(out.value));
}

TEST_F(RespParserTest, ParsesArrayAndConvertsStringElementsToCommand) {
    const std::string request = "*2\r\n$3\r\nGET\r\n$1\r\nk\r\n";
    EXPECT_EQ(parse_one(parser, request, out), ParseResult::Success);
    ASSERT_TRUE(std::holds_alternative<RESPArray>(out.value));
    EXPECT_EQ(parser.get_command_array(out),
              (std::vector<std::string>{"GET", "k"}));
}

TEST_F(RespParserTest, IncompleteFrameCanBeCompletedByLaterFeed) {
    const std::string prefix = "$5\r\nhello";
    const std::string suffix = "\r\n";
    parser.feed(prefix.data(), prefix.size());
    EXPECT_EQ(parser.try_parse(out), ParseResult::Failure);
    parser.feed(suffix.data(), suffix.size());
    EXPECT_EQ(parser.try_parse(out), ParseResult::Success);
    ASSERT_TRUE(std::holds_alternative<std::string>(out.value));
    EXPECT_EQ(std::get<std::string>(out.value), "hello");
}

TEST_F(RespParserTest, ParsesMultipleFramesFromOneFeed) {
    const std::string pipeline = "+OK\r\n:7\r\n$1\r\nx\r\n";
    parser.feed(pipeline.data(), pipeline.size());
    ASSERT_EQ(parser.try_parse(out), ParseResult::Success);
    ASSERT_EQ(parser.try_parse(out), ParseResult::Success);
    ASSERT_EQ(parser.try_parse(out), ParseResult::Success);
    EXPECT_EQ(parser.try_parse(out), ParseResult::Failure);
}

TEST_F(RespParserTest, ParsesMaximumConfiguredBulkPayload) {
    const std::string payload(64, 'x');
    const std::string frame = "$64\r\n" + payload + "\r\n";
    EXPECT_EQ(parse_one(parser, frame, out), ParseResult::Success);
    ASSERT_TRUE(std::holds_alternative<std::string>(out.value));
    EXPECT_EQ(std::get<std::string>(out.value), payload);
}

TEST_F(RespParserTest, RejectsInvalidTypeAndConsumesMalformedInput) {
    EXPECT_EQ(parse_one(parser, "?\r\n", out), ParseResult::Rejection);
    ASSERT_TRUE(std::holds_alternative<RESPArray>(out.value));
    EXPECT_EQ(parser.try_parse(out), ParseResult::Failure);
}

TEST_F(RespParserTest, RejectsInvalidIntegerSyntax) {
    EXPECT_EQ(parse_one(parser, ":12x\r\n", out), ParseResult::Rejection);
}

TEST_F(RespParserTest, RejectsNegativeLengthsOtherThanNullSentinel) {
    EXPECT_EQ(parse_one(parser, "$-2\r\n", out), ParseResult::Rejection);
}

TEST_F(RespParserTest, RejectsConfiguredBulkLengthLimit) {
    const std::string payload(65, 'x');
    const std::string frame = "$65\r\n" + payload + "\r\n";

    EXPECT_EQ(parse_one(parser, frame, out), ParseResult::Rejection);
}

TEST_F(RespParserTest, RejectsConfiguredArrayElementLimit) {
    EXPECT_EQ(parse_one(parser, "*5\r\n", out), ParseResult::Rejection);
}

TEST_F(RespParserTest, RejectsConfiguredLineLengthLimit) {
    const std::string frame = "+" + std::string(65, 'x') + "\r\n";
    EXPECT_EQ(parse_one(parser, frame, out), ParseResult::Rejection);
}

TEST_F(RespParserTest, RejectsNestingBeyondConfiguredLimit) {
    EXPECT_EQ(parse_one(parser, "*1\r\n*1\r\n:1\r\n", out), ParseResult::Rejection);
}
