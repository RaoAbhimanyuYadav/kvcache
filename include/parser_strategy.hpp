#pragma once

#include <iostream>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>


struct RESPObj;
using RESPArray = std::vector<RESPObj>;

struct RESPObj{
    std::variant<std::string, RESPArray, int64_t, std::nullptr_t> value;
};

// Helper utility to match types in std::visit
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;




class ParserStrategy{
    public:
    ParserStrategy():pos(0){}
    virtual void feed(const char* chunk, std::size_t size) = 0;
    virtual bool try_parse(RESPObj &out_obj) = 0;
    virtual void  extract_resp_obj(const RESPObj &obj, int depth=0) const = 0;
    virtual std::vector<std::string> get_command_array(const RESPObj &obj) const = 0;
    void clear(){
        data.clear();
        pos = 0;
    }
    protected:
    std::string data;
    size_t pos;

};
