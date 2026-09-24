#include "store.hpp"

bool Store::set_key_value(const std::string& key, const std::string& val){
    if(key.size() > 10 || val.size() > 20) return false;
    store[key] = val;
    return true;
}

std::pair<bool, std::string> Store::get_value(const std::string& key){
    if(store.find(key) == store.end()) return std::make_pair(false, "No key found");
    return std::make_pair(true, store[key]);
}

bool Store::del_key_val(const std::string& key){
    return store.erase(key);
}

bool Store::exists_key(const std::string& key){
    if( store.find(key) == store.end()) return false;
    return true;
}