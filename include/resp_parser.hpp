#pragma once

#include <iostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <stdexcept>
#include <cassert>

#include "parser_strategy.hpp"

class IncompleteFrameException : public std::runtime_error{ 
    public:
    IncompleteFrameException():std::runtime_error("Incomplete Resp Frame"){}
};

class MalformedFrameException : public std::runtime_error{
    public:
    MalformedFrameException(const std::string&& err) : std::runtime_error(err){}
};

class RESPParser:public ParserStrategy{
    public:
    explicit RESPParser();

    void feed(const char* chunk, uint size) override;

    bool try_parse(RESPObj &out_obj) override;

    void  extract_resp_obj(const RESPObj &obj, int depth=0) const override;

    std::vector<std::string> get_command_array(const RESPObj &obj) const;

    private:

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