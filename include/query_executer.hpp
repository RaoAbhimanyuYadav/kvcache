#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <array>
#include <iostream>

#include <stdexcept>
#include <cassert>

#include "store.hpp"
#include "global.hpp"



class InvalidNumberOfArgumentsExecption : public std::runtime_error{ 
    public:
    InvalidNumberOfArgumentsExecption():std::runtime_error("-ERR Wrong Number of argument.\r\n"){}
};

class InvalidArgumentsLengthExecption : public std::runtime_error{ 
    public:
    InvalidArgumentsLengthExecption():std::runtime_error("-ERR Invalid argument Size.\r\n"){}
};

class FailedToExecuteExecption : public std::runtime_error{ 
    public:
    FailedToExecuteExecption(const std::string_view err):std::runtime_error("-ERR Unsupported Command \'" + (std::string)err +"\'.\r\n"){}
};


constexpr std::array<std::string_view, 5> SUPPORTED_COMMAND = {"PING", "GET", "SET", "EXISTS", "DEL"};

class QueryExecuter{
    public:
    std::string execute(const std::vector<std::string> &args);

    private:
    std::string try_execution(const std::vector<std::string> &args);
    Store store;
};
