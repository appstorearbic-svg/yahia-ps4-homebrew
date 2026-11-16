#pragma once
#include <string>
#include <vector>
namespace gtxinstall {
typedef void (*StatusCallback)(const char *message, void *context);
struct Result { std::string text; bool ok; };
bool orbxReady(std::string *reason = nullptr);
bool installZip(const std::string &zipPath, const std::string &modId,
                StatusCallback status, void *context,
                std::vector<Result> &results, std::string *error = nullptr);
}
