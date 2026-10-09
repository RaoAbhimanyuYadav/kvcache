#pragma once

#include <unordered_map>
#include <utility>
#include <string>

#include "global.hpp"

class Store{
    public:
    bool set_key_value(const std::string& key, const std::string& val);

    std::pair<bool, std::string> get_value(const std::string& key);

    bool del_key_val(const std::string& key);
    
    bool exists_key(const std::string& key);

    private:
    std::unordered_map<std::string, std::string> store;
};
