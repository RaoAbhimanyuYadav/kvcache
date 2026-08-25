#pragma once

#include <iostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <stdexcept>
#include <cassert>

struct RESPObj;
using RESPArray = std::vector<RESPObj>;

struct RESPObj{
    std::variant<std::string, RESPArray, int64_t, std::nullptr_t> value;
};

class IncompleteFrameException : public std::runtime_error{ 
    public:
    IncompleteFrameException():std::runtime_error("Incomplete Resp Frame"){}
};

class RESPParser{
    public:
    explicit RESPParser();

    void feed(std::string_view chunk);

    bool try_parse(RESPObj &out_obj);

    void clear();

    private:
    std::string data;
    size_t pos;

    RESPObj parse();

    /* carriage return line feed*/
    std::string_view read_until_crlf();

    std::string parse_simple_string();
    std::string parse_error();
    int64_t parse_integer();
    RESPObj parse_bulk_string();
    RESPObj parse_array();
};

void print_resp(const RESPObj &obj);