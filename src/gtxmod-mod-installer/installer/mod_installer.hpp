#pragma once
#include <string>
#include <vector>
#include <stdint.h>
namespace gtxinstall {
typedef void (*StatusCallback)(const char *message, void *context);
typedef bool (*ProgressCallback)(uint64_t current, uint64_t total, void *context);
struct Result { std::string text; bool ok; };
struct InstalledMod { std::string id,name; std::vector<std::string> paths,xmlLines; };
bool orbxReady(std::string *reason = nullptr);
bool installOrbx(const std::string &zipPath, StatusCallback status, ProgressCallback progress, void *context,
                std::vector<Result> &results, std::string *error = nullptr);
bool installZip(const std::string &zipPath,const std::string &modId,
                StatusCallback status, void *context,
                std::vector<Result> &results, std::string *error = nullptr);
bool listInstalled(std::vector<InstalledMod> &mods, std::string *error = nullptr);
bool removeInstalled(const InstalledMod &mod, std::vector<Result> &results, std::string *error = nullptr);
}
