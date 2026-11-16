#pragma once
#include <map>
#include <string>
#include <vector>

namespace gtxjson {
struct Value {
    enum Type { Null, Boolean, Number, String, Array, Object } type = Null;
    std::string text;
    std::vector<Value> array;
    std::map<std::string, Value> object;
    const Value *get(const std::string &key) const;
    std::string string(const std::string &fallback = "") const;
    bool boolean(bool fallback = false) const;
};
bool parse(const std::string &input, Value &out, std::string *error = nullptr);
}
