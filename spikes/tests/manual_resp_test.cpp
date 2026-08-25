#include <resp_parser.hpp>

void assert_string(const RESPObj &obj, const std::string& expected){
    assert(std::holds_alternative<std::string>(obj.value));
    assert(std::get<std::string>(obj.value) == expected);
}

void assert_integer(const RESPObj&obj, int64_t expected){
    assert(std::holds_alternative<int64_t>(obj.value));
    assert(expected == std::get<int64_t>(obj.value));
}

void assert_null(const RESPObj&obj){
    assert(std::holds_alternative<std::nullptr_t>(obj.value));
}


int main() {
    RESPParser parser;
    RESPObj result;

    // Test 1: Simple String
    std::cout << "[RUNNING] Test 1: Simple String... " << std::flush;
    parser.feed("+OK\r\n");
    assert(parser.try_parse(result));
    assert_string(result, "OK");
    std::cout << "PASSED\n";
    parser.clear();

    // Test 2: Error Frame
    std::cout << "[RUNNING] Test 2: Error Frame... " << std::flush;
    parser.feed("-ERR unknown command\r\n");
    assert(parser.try_parse(result));
    assert_string(result, "Error: ERR unknown command");
    std::cout << "PASSED\n";
    parser.clear();

    // Test 3: Integer Frame
    std::cout << "[RUNNING] Test 3: Integer Frame... " << std::flush;
    parser.feed(":42\r\n");
    assert(parser.try_parse(result));
    assert_integer(result, 42);
    std::cout << "PASSED\n";
    parser.clear();

    // Test 4: Bulk String
    std::cout << "[RUNNING] Test 4: Bulk String... " << std::flush;
    parser.feed("$5\r\nhello\r\n");
    assert(parser.try_parse(result));
    assert_string(result, "hello");
    std::cout << "PASSED\n";
    parser.clear();

    // Test 5: Null Bulk String
    std::cout << "[RUNNING] Test 5: Null Bulk String... " << std::flush;
    parser.feed("$-1\r\n");
    assert(parser.try_parse(result));
    assert_null(result);
    std::cout << "PASSED\n";
    parser.clear();

    // Test 6: Stream Fragmentation (Part 1 - Expecting Incomplete)
    std::cout << "[RUNNING] Test 6a: Partial Data (First Half)... " << std::flush;
    std::string first_half  = "*3\r\n$3\r\nSET\r\n$3\r\nfo"; 
    parser.feed(first_half);
    bool process_status_1 = parser.try_parse(result);
    assert(process_status_1 == false); // Must return false (incomplete)
    std::cout << "PASSED (Correctly identified as incomplete)\n";

    // Test 7: Stream Fragmentation (Part 2 - Expecting Success)
    std::cout << "[RUNNING] Test 6b: Appending Remaining Data (Second Half)... " << std::flush;
    std::string second_half = "o\r\n$3\r\nbar\r\n";          
    parser.feed(second_half);
    bool process_status_2 = parser.try_parse(result);
    assert(process_status_2 == true); // Must return true now

    // Structurally validate the completed array contents
    assert(std::holds_alternative<RESPArray>(result.value));
    RESPArray cmd_tokens = std::get<RESPArray>(result.value);
    assert(cmd_tokens.size() == 3);
    assert_string(cmd_tokens[0], "SET");
    assert_string(cmd_tokens[1], "foo");
    assert_string(cmd_tokens[2], "bar");
    std::cout << "PASSED (Array fully restored and validated)\n";

    std::cout << "\n=========================================\n";
    std::cout << "🎉 ALL UNIT TESTS PASSED SUCCESSFULLY! 🎉\n";
    std::cout << "=========================================\n";
    
    return 0;
}
