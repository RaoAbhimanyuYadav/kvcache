#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <array>
#include <iostream>

constexpr std::array<std::string_view, 5> SUPPORTED_COMMAND = {"PING", "GET", "SET", "EXISTS", "DEL"};

class QueryExecuter{
    public:
    bool execute(const std::vector<std::string> &query_array, std::string &resp){
        if(query_array.size() == 0) return false;
        const std::string& cmd = query_array[0];
        if(cmd == "PING"){
            resp = "+PONG\r\n";
            return true;
        }
        if(cmd == "SET") {
            resp = "+OK\r\n";
            return true;
        }
        if(cmd == "GET"){
            resp = "$4\r\npong\r\n";
            return true;
            resp = "$-1\r\n";
            return true;
        }
        if(cmd == "DEL"){
            resp = ":1\r\n";
            return true;
            resp = ":0\r\n";
            return true;
        }
        if(cmd == "EXISTS"){
            resp = ":1\r\n";
            return true;
            resp = ":0\r\n";
            return true;
        }
        if(cmd == "ERR"){
            resp = "-ERR "+ query_array[1] +  "\r\n";
            std::cout<<resp;
            return false;
        }
        resp = "-ERR Unsupported Command\r\n";
        return true;
    }
};
