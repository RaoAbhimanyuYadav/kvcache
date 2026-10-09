#include "store.hpp"

bool Store::set_key_value(const std::string& key, const std::string& val){
    if(key.size() > KEY_MAX_SIZE || val.size() > VALUE_MAX_SIZE) return false;
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