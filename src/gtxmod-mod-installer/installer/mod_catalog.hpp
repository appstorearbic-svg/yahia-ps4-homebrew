#pragma once
#include <string>
#include <vector>
namespace gtxcatalog {
struct Mod { std::string id,name,category,description,image,download,size; };
extern const char *const categories[];
extern const size_t categoryCount;
std::string resolveUrl(const std::string &path);
bool fetchMods(std::vector<Mod> &mods,std::string *error=nullptr);
}
